// CUDA entry point for flinch_kernels.inc. Compiled to PTX at build time and
// loaded through the CUDA driver API, so the plugin has no CUDA runtime dependency.
#define DEVICE __device__ __forceinline__
#define PRIV
#define GBUF_CONST const float4*
#define GBUF float4*
#define F4(a, b, c, d) make_float4((a), (b), (c), (d))
#define KERNEL1(name, P) \
    extern "C" __global__ void name(GBUF_CONST src, GBUF dst, P p) { \
        const int x = int(blockIdx.x * blockDim.x + threadIdx.x); \
        const int y = int(blockIdx.y * blockDim.y + threadIdx.y);
#define KERNEL2(name, P) \
    extern "C" __global__ void name(GBUF_CONST src, GBUF_CONST src2, GBUF dst, P p) { \
        const int x = int(blockIdx.x * blockDim.x + threadIdx.x); \
        const int y = int(blockIdx.y * blockDim.y + threadIdx.y);
#define KERNEL_END }

#include "flinch_gpu_types.h"
#include "flinch_kernels.inc"
