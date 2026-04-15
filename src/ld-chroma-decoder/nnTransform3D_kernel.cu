#include <cuda_runtime.h>
#include <cufft.h>
#include <onnxruntime_cxx_api.h>



// GPU kernel
__global__ void applyMaskKernel(cufftDoubleComplex* d_out_batch, const float* d_mask, int total_elements) {
    // Compute global thread index
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    
    // Defensive bounds check
    if (idx < total_elements) {
        float gain = d_mask[idx];
        d_out_batch[idx].x *= gain;
        d_out_batch[idx].y *= gain;
    }
}

// GPU kernel: compute Magnitude and reflected feature map (RefMag)
__global__ void calcMagnitudeKernel(const cufftDoubleComplex* d_out_batch, float* d_trt_input, int num_blocks, int block_size) {
    int idx = blockIdx.x * blockDim.x + threadIdx.x;
    if (idx >= num_blocks * block_size) return;

    int b = idx / block_size;  // Block index
    int i = idx % block_size;  // Offset inside block

    // Decode tensor coordinates
    int Nx = 16, Ny = 16, Nt = 4;
    int t = i / (Ny * Nx);
    int rem = i % (Ny * Nx);
    int y = rem / Nx;
    int x = rem % Nx;

    // Channel 0: Mag
    double mag = sqrt(d_out_batch[idx].x * d_out_batch[idx].x + d_out_batch[idx].y * d_out_batch[idx].y);

    // Channel 1: RefMag (reflected coordinates)
    int ref_t = (2 - t) % 4; if (ref_t < 0) ref_t += 4;
    int ref_y = (16 - y) % 16;
    int ref_x = (8 - x) % 16; if (ref_x < 0) ref_x += 16;
    int idx_ref = b * block_size + (ref_t * Ny * Nx + ref_y * Nx + ref_x);
    double mag_ref = sqrt(d_out_batch[idx_ref].x * d_out_batch[idx_ref].x + d_out_batch[idx_ref].y * d_out_batch[idx_ref].y);

    // Write into TensorRT input tensor memory
    // Data shape: [num_blocks, 2, Nt, Ny, Nx]
    int out_idx_0 = b * (2 * block_size) + 0 * block_size + i; // Channel 0 offset
    int out_idx_1 = b * (2 * block_size) + 1 * block_size + i; // Channel 1 offset

    // Apply normalization
    d_trt_input[out_idx_0] = (float)(mag / 128.0);
    d_trt_input[out_idx_1] = (float)(mag_ref / 128.0);
}

// Kernel 1: each thread computes the DC (mean) for one block
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

// Kernel 2: remove DC, apply window, and pack in one pass
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

// Kernel 3: safe overlap-add accumulation (OLA) in device memory
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

        // Atomic add: prevents write conflicts when many threads target the same pixel
        atomicAdd((t < 2) ? &d_accChroma_f0[frame_idx] : &d_accChroma_f1[frame_idx], val * w);
        atomicAdd((t < 2) ? &d_weightSum_f0[frame_idx] : &d_weightSum_f1[frame_idx], w * w);
    }
}












// C++ wrapper entry point
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
    int Nt = 4, Ny = 16, Nx = 16; // Keep this consistent with the network structure

    // 1. Compute DC
    calcDCKernel<<<blocksForDC, 256>>>(d_cvbs_f0, d_cvbs_f1, pad_f0, pad_f1, 
                                       d_ledger_y, d_ledger_x, d_ledger_dc, num_blocks, activeStartX, activeEndX);

    // 2. Pack and apply window
    packAndWindowKernel<<<blocksForAll, 256>>>(d_cvbs_f0, d_cvbs_f1, pad_f0, pad_f1, d_in_batch,
                                               d_ledger_y, d_ledger_x, d_ledger_dc, d_winT, d_winY, d_winX,
                                               num_blocks, block_size, activeStartX, activeEndX);
    
    // 3. Forward cuFFT
    cufftExecZ2Z(p_fwd, d_in_batch, d_out_batch, CUFFT_FORWARD);

    // 4. Compute magnitude + reflected features (for TensorRT)
    calcMagnitudeKernel<<<blocksForAll, 256>>>(d_out_batch, d_trt_input, num_blocks, block_size);
    cudaDeviceSynchronize(); // Ensure GPU writes are complete before inference

    // 5. TensorRT IOBinding inference (fully in device memory)
    Ort::IoBinding iobinding(*session);
    Ort::MemoryInfo mem_info_cuda("Cuda", OrtDeviceAllocator, 0, OrtMemTypeDefault);
    
    std::vector<int64_t> input_shape = { num_blocks, 2, Nt, Ny, Nx };
    Ort::Value input_tensor = Ort::Value::CreateTensor<float>(mem_info_cuda, d_trt_input, num_blocks * 2 * block_size, input_shape.data(), input_shape.size());
    iobinding.BindInput("input", input_tensor);

    std::vector<int64_t> output_shape = { num_blocks, 1, Nt, Ny, Nx };
    Ort::Value output_tensor = Ort::Value::CreateTensor<float>(mem_info_cuda, d_mask, total_elements, output_shape.data(), output_shape.size());
    iobinding.BindOutput("output", output_tensor);

    session->Run(Ort::RunOptions{ nullptr }, iobinding);

    // 6. Apply mask
    applyMaskKernel<<<blocksForAll, 256>>>(d_out_batch, d_mask, total_elements);

    // 7. Inverse cuFFT
    cufftExecZ2Z(p_inv, d_out_batch, d_in_batch, CUFFT_INVERSE);

    // 8. OLA accumulation
    olaKernel<<<blocksForAll, 256>>>(d_in_batch, d_accChroma_f0, d_accChroma_f1, d_weightSum_f0, d_weightSum_f1,
                                     pad_f0, pad_f1, d_ledger_y, d_ledger_x, d_winT, d_winY, d_winX,
                                     num_blocks, block_size, activeStartX, activeEndX);
    cudaDeviceSynchronize();
}