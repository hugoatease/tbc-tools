#include <cuda_runtime.h>
#include <cufft.h>
#include <onnxruntime_cxx_api.h>



// 这是一个运行在 GPU 上的核函数
__global__ void applyMaskKernel(cufftDoubleComplex* d_out_batch, const float* d_mask, int total_elements) {
    // 获取当前 GPU 线程的全局唯一 ID
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    
    // 防御性边界检查
    if (idx < total_elements) {
        float gain = d_mask[idx];
        d_out_batch[idx].x *= gain;
        d_out_batch[idx].y *= gain;
    }
}

// 运行在 GPU 上的核函数：极速计算 Magnitude 和对称特征 (RefMag)
__global__ void calcMagnitudeKernel(const cufftDoubleComplex* d_out_batch, float* d_trt_input, int num_blocks, int block_size) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= num_blocks * block_size) return;

    int b = idx / block_size;  // 当前处于第几个 block
    int i = idx % block_size;  // 当前在 block 内的偏移量

    // 维度解码
    int Nx = 16, Ny = 16, Nt = 4;
    int t = i / (Ny * Nx);
    int rem = i % (Ny * Nx);
    int y = rem / Nx;
    int x = rem % Nx;

    // Channel 0: Mag
    double mag = sqrt(d_out_batch[idx].x * d_out_batch[idx].x + d_out_batch[idx].y * d_out_batch[idx].y);

    // Channel 1: RefMag (处理对称翻转)
    int ref_t = (2 - t) % 4; if (ref_t < 0) ref_t += 4;
    int ref_y = (16 - y) % 16;
    int ref_x = (8 - x) % 16; if (ref_x < 0) ref_x += 16;
    int idx_ref = b * block_size + (ref_t * Ny * Nx + ref_y * Nx + ref_x);
    double mag_ref = sqrt(d_out_batch[idx_ref].x * d_out_batch[idx_ref].x + d_out_batch[idx_ref].y * d_out_batch[idx_ref].y);

    // 写入 TensorRT 输入张量显存
    // 数据形状: [num_blocks, 2, Nt, Ny, Nx]
    int out_idx_0 = b * (2 * block_size) + 0 * block_size + i; // Channel 0 偏移
    int out_idx_1 = b * (2 * block_size) + 1 * block_size + i; // Channel 1 偏移

    // 顺手做掉除以 65536.0 的归一化操作
    d_trt_input[out_idx_0] = (float)(mag / 128.0);
    d_trt_input[out_idx_1] = (float)(mag_ref / 128.0);
}

// 核函数 1：让 5000 个线程各自算自己 Block 的 DC (平均值)
__global__ void calcDCKernel(const uint16_t* d_cvbs_f0, const uint16_t* d_cvbs_f1, bool pad_f0, bool pad_f1,
                             const int* d_ledger_y, const int* d_ledger_x, double* d_ledger_dc, 
                             int num_blocks, int activeStartX, int activeEndX) {
    int b = blockIdx.x * blockDim.x + threadIdx.x;
    if (b >= num_blocks) return;

    int y = d_ledger_y[b]; int x = d_ledger_x[b];
    double blockDC = 0.0; int pixelCount = 0;

    for (int t = 0; t < 4; ++t) {
        if ((t < 2) ? pad_f0 : pad_f1) continue;
        const uint16_t* cvbs = (t < 2) ? d_cvbs_f0 : d_cvbs_f1;
        bool isOddField = (t % 2 != 0);

        for (int dy = 0; dy < 16; ++dy) {
            int absY = y + dy;
            if (absY < 40 || absY >= 525) continue;
            if ((absY % 2 != 0) == isOddField) {
                for (int dx = 0; dx < 16; ++dx) {
                    int absX = x + dx;
                    if (absX >= activeStartX && absX < activeEndX) {
                        blockDC += cvbs[absY * 910 + absX];
                        pixelCount++;
                    }
                }
            }
        }
    }
    d_ledger_dc[b] = (pixelCount > 0) ? (blockDC / pixelCount) : 0.0;
}

// 核函数 2：瞬间完成去直流、加窗、打包
__global__ void packAndWindowKernel(const uint16_t* d_cvbs_f0, const uint16_t* d_cvbs_f1, bool pad_f0, bool pad_f1,
                                    cufftDoubleComplex* d_in_batch, const int* d_ledger_y, const int* d_ledger_x, const double* d_ledger_dc,
                                    const double* d_winT, const double* d_winY, const double* d_winX,
                                    int num_blocks, int block_size, int activeStartX, int activeEndX) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= num_blocks * block_size) return;

    int b = idx / block_size; int i = idx % block_size;
    int t = i / 256; int rem = i % 256; int dy = rem / 16; int dx = rem % 16;

    int absY = d_ledger_y[b] + dy;
    int absX = d_ledger_x[b] + dx;
    bool isOddField = (t % 2 != 0);
    bool isPad = (t < 2) ? pad_f0 : pad_f1;
    
    if (isPad || absY < 40 || absY >= 525 || absX < activeStartX || absX >= activeEndX || (absY % 2 != 0) != isOddField) {
        d_in_batch[idx].x = 0.0;
    } else {
        const uint16_t* cvbs = (t < 2) ? d_cvbs_f0 : d_cvbs_f1;
        double val = (double)cvbs[absY * 910 + absX];
        d_in_batch[idx].x = (val - d_ledger_dc[b]) * d_winT[t] * d_winY[dy] * d_winX[dx];
    }
    d_in_batch[idx].y = 0.0;
}

// 核函数 3：显存内安全叠接相加 (OLA)
__global__ void olaKernel(const cufftDoubleComplex* d_in_batch, double* d_accChroma_f0, double* d_accChroma_f1,
                          double* d_weightSum_f0, double* d_weightSum_f1, bool pad_f0, bool pad_f1,
                          const int* d_ledger_y, const int* d_ledger_x, const double* d_winT, const double* d_winY, const double* d_winX,
                          int num_blocks, int block_size, int activeStartX, int activeEndX) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= num_blocks * block_size) return;

    int b = idx / block_size; int i = idx % block_size;
    int t = i / 256; int rem = i % 256; int dy = rem / 16; int dx = rem % 16;

    if ((t < 2) ? pad_f0 : pad_f1) return;

    int absY = d_ledger_y[b] + dy;
    int absX = d_ledger_x[b] + dx;

    if (absY >= 40 && absY < 525 && (absY % 2 != 0) == (t % 2 != 0) && absX >= activeStartX && absX < activeEndX) {
        double val = d_in_batch[idx].x / (double)block_size;
        double w = d_winT[t] * d_winY[dy] * d_winX[dx];
        int frame_idx = absY * 910 + absX;

        // 原子加法：防止几万个线程同时写入同一个像素发生冲突！
        atomicAdd((t < 2) ? &d_accChroma_f0[frame_idx] : &d_accChroma_f1[frame_idx], val * w);
        atomicAdd((t < 2) ? &d_weightSum_f0[frame_idx] : &d_weightSum_f1[frame_idx], w * w);
    }
}












// C++ 端调用的 Wrapper
void launch_nnTransform3D_CUDA(
    uint16_t* d_cvbs_f0, uint16_t* d_cvbs_f1, bool pad_f0, bool pad_f1,
    double* d_accChroma_f0, double* d_accChroma_f1, double* d_weightSum_f0, double* d_weightSum_f1,
    cufftHandle p_fwd, cufftHandle p_inv, cufftDoubleComplex* d_in_batch, cufftDoubleComplex* d_out_batch,
    float* d_trt_input, float* d_mask,
    int* d_ledger_y, int* d_ledger_x, double* d_ledger_dc, 
    double* d_winX, double* d_winY, double* d_winT,
    int num_blocks, size_t block_size, int activeStartX, int activeEndX,
    Ort::Session* session) 
{
    int blocksForDC = (num_blocks + 255) / 256;
    int total_elements = num_blocks * block_size;
    int blocksForAll = (total_elements + 255) / 256;
    int Nt = 4, Ny = 16, Nx = 16; // 这里保持与网络结构一致

    // 1. 计算 DC
    calcDCKernel<<<blocksForDC, 256>>>(d_cvbs_f0, d_cvbs_f1, pad_f0, pad_f1, 
                                       d_ledger_y, d_ledger_x, d_ledger_dc, num_blocks, activeStartX, activeEndX);

    // 2. 打包与加窗
    packAndWindowKernel<<<blocksForAll, 256>>>(d_cvbs_f0, d_cvbs_f1, pad_f0, pad_f1, d_in_batch,
                                               d_ledger_y, d_ledger_x, d_ledger_dc, d_winT, d_winY, d_winX,
                                               num_blocks, block_size, activeStartX, activeEndX);
    
    // 3. 正向 cuFFT
    cufftExecZ2Z(p_fwd, d_in_batch, d_out_batch, CUFFT_FORWARD);

    // 4. 计算幅度谱与对称特征 (供给 TensorRT)
    calcMagnitudeKernel<<<blocksForAll, 256>>>(d_out_batch, d_trt_input, num_blocks, block_size);
    cudaDeviceSynchronize(); // 确保数据写入显存完毕，准备推理

    // 5. TensorRT IOBinding 推理 (完全在显存中进行)
    Ort::IoBinding iobinding(*session);
    Ort::MemoryInfo mem_info_cuda("Cuda", OrtDeviceAllocator, 0, OrtMemTypeDefault);
    
    std::vector<int64_t> input_shape = { num_blocks, 2, Nt, Ny, Nx };
    Ort::Value input_tensor = Ort::Value::CreateTensor<float>(mem_info_cuda, d_trt_input, num_blocks * 2 * block_size, input_shape.data(), input_shape.size());
    iobinding.BindInput("input", input_tensor);

    std::vector<int64_t> output_shape = { num_blocks, 1, Nt, Ny, Nx };
    Ort::Value output_tensor = Ort::Value::CreateTensor<float>(mem_info_cuda, d_mask, total_elements, output_shape.data(), output_shape.size());
    iobinding.BindOutput("output", output_tensor);

    session->Run(Ort::RunOptions{ nullptr }, iobinding);

    // 6. 应用掩膜
    applyMaskKernel<<<blocksForAll, 256>>>(d_out_batch, d_mask, total_elements);

    // 7. 逆向 cuFFT
    cufftExecZ2Z(p_inv, d_out_batch, d_in_batch, CUFFT_INVERSE);

    // 8. OLA 叠接相加
    olaKernel<<<blocksForAll, 256>>>(d_in_batch, d_accChroma_f0, d_accChroma_f1, d_weightSum_f0, d_weightSum_f1,
                                     pad_f0, pad_f1, d_ledger_y, d_ledger_x, d_winT, d_winY, d_winX,
                                     num_blocks, block_size, activeStartX, activeEndX);
    cudaDeviceSynchronize();
}