#include <cuda_runtime.h>
#include <device_launch_parameters.h>
#include <core/system/Vector.hpp>

#ifdef _WIN32
#include <windows.h>
#endif
#include <cuda_gl_interop.h>

#define CUDA_SAFE_CALL_ALWAYS(A)              \
    A;                                        \
    cudaDeviceSynchronize();                  \
    if (cudaPeekAtLastError() != cudaSuccess) \
        SIBR_ERR << cudaGetErrorString(cudaGetLastError());

#if DEBUG || _DEBUG
#define CUDA_SAFE_CALL(A) CUDA_SAFE_CALL_ALWAYS(A)
#else
#define CUDA_SAFE_CALL(A) A
#endif