/* OpenCL C prelude for flinch_kernels.inc. */
#define DEVICE
#define PRIV
#define GBUF_CONST __global const float4*
#define GBUF __global float4*
#define F4(a, b, c, d) ((float4)((a), (b), (c), (d)))
#define KERNEL1(name, P) \
    __kernel void name(GBUF_CONST src, GBUF dst, P p) { \
        const int x = (int)get_global_id(0); \
        const int y = (int)get_global_id(1);
#define KERNEL2(name, P) \
    __kernel void name(GBUF_CONST src, GBUF_CONST src2, GBUF dst, P p) { \
        const int x = (int)get_global_id(0); \
        const int y = (int)get_global_id(1);
#define KERNEL_END }
