# ==========================================
# 1. 启用 CUDA 支持与架构配置
# ==========================================
# 在子模块中动态启用 CUDA 语言
enable_language(CUDA)

# 设定现代 GPU 架构 (75=20系, 86=30系, 89=40系)，防止 atomicAdd 报错
set(CMAKE_CUDA_ARCHITECTURES 75 86 89)
set(CMAKE_CUDA_STANDARD 17)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)

# ==========================================
# 2. 查找外部深度学习与 GPU 依赖
# ==========================================
find_package(CUDAToolkit REQUIRED)

find_path(ONNXRUNTIME_INCLUDE_DIR NAMES onnxruntime_cxx_api.h PATHS ${ONNXRUNTIME_ROOT}/include $ENV{ONNXRUNTIME_ROOT}/include)
find_library(ONNXRUNTIME_LIBRARY NAMES onnxruntime PATHS ${ONNXRUNTIME_ROOT}/lib $ENV{ONNXRUNTIME_ROOT}/lib)

if(NOT ONNXRUNTIME_INCLUDE_DIR OR NOT ONNXRUNTIME_LIBRARY)
    message(FATAL_ERROR "Could not find ONNX Runtime. Please set ONNXRUNTIME_ROOT.")
endif()

# ==========================================
# 3. 原始构建逻辑 (融合 GPU 滤镜)
# ==========================================
# For M_PI constant
add_compile_definitions(_USE_MATH_DEFINES)

# Embed nnTransform3D model data
set(_nn_model_file "${CMAKE_CURRENT_SOURCE_DIR}/resources/chroma_net_v2.onnx")
if(NOT EXISTS "${_nn_model_file}")
    message(FATAL_ERROR "Missing nnTransform3D model: ${_nn_model_file}")
endif()
file(READ "${_nn_model_file}" _nn_model_hex HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," _nn_model_array "${_nn_model_hex}")
string(LENGTH "${_nn_model_hex}" _nn_model_hex_len)
math(EXPR _nn_model_size "${_nn_model_hex_len} / 2")
set(_nn_model_header "${CMAKE_CURRENT_BINARY_DIR}/chroma_net_v2_onnx_data.h")
set(_nn_model_source "${CMAKE_CURRENT_BINARY_DIR}/chroma_net_v2_onnx_data.cpp")
file(WRITE "${_nn_model_header}"
    "#pragma once\n"
    "#include <cstddef>\n"
    "extern const unsigned char kChromaNetV2OnnxData[];\n"
    "extern const std::size_t kChromaNetV2OnnxDataSize;\n")
file(WRITE "${_nn_model_source}"
    "#include \"chroma_net_v2_onnx_data.h\"\n"
    "const unsigned char kChromaNetV2OnnxData[] = {${_nn_model_array}};\n"
    "const std::size_t kChromaNetV2OnnxDataSize = ${_nn_model_size};\n")

add_library(lddecode-chroma STATIC
    decoder.cpp
    decoderpool.cpp
    comb.cpp
    componentframe.cpp
    framecanvas.cpp
    outputwriter.cpp
    palcolour.cpp
    sourcefield.cpp
    transformpal.cpp
    transformpal2d.cpp
    transformpal3d.cpp
    monodecoder.cpp
    nnTransform3D_kernel.cu
    ${_nn_model_source}
)

target_include_directories(lddecode-chroma PUBLIC
    .
    ${CMAKE_CURRENT_BINARY_DIR}
    ${ONNXRUNTIME_INCLUDE_DIR}
)
target_link_libraries(lddecode-chroma PRIVATE
    Qt::Core
    ${FFTW_LIBRARY}
    onnxruntime::onnxruntime
    lddecode-library
    CUDA::cufft
    CUDA::cudart
)

target_include_directories(lddecode-chroma PRIVATE ${FFTW_INCLUDE_DIR})

# ==========================================
# 4. 可执行文件构建 (ld-chroma-decoder)
# ==========================================
add_executable(ld-chroma-decoder
    decoder.cpp
    decoderpool.cpp
    main.cpp
    monodecoder.cpp
    ntscdecoder.cpp
    paldecoder.cpp
)

set_target_properties(lddecode-chroma PROPERTIES AUTOMOC ON)

target_include_directories(ld-chroma-decoder PRIVATE ${FFTW_INCLUDE_DIR})
target_link_libraries(ld-chroma-decoder PRIVATE
    Qt::Core
    ${FFTW_LIBRARY}
    onnxruntime::onnxruntime
    lddecode-library
    lddecode-chroma
    CUDA::cufft
    CUDA::cudart
)

install(TARGETS ld-chroma-decoder)