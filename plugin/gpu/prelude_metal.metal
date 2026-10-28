// Metal Shading Language prelude for flinch_kernels.inc.
#include <metal_stdlib>
using namespace metal;

#define DEVICE static inline
#define PRIV thread
#define GBUF_CONST device const float4*
#define GBUF device float4*
#define F4(a, b, c, d) float4((a), (b), (c), (d))
#define KERNEL1(name, P) \
    kernel void name(GBUF_CONST src [[buffer(0)]], GBUF dst [[buffer(1)]], \
                     constant P& p_in [[buffer(2)]], uint2 gid [[thread_position_in_grid]]) { \
        const P p = p_in; \
        const int x = int(gid.x); \
        const int y = int(gid.y);
#define KERNEL2(name, P) \
    kernel void name(GBUF_CONST src [[buffer(0)]], GBUF_CONST src2 [[buffer(1)]], \
                     GBUF dst [[buffer(2)]], constant P& p_in [[buffer(3)]], \
                     uint2 gid [[thread_position_in_grid]]) { \
        const P p = p_in; \
        const int x = int(gid.x); \
        const int y = int(gid.y);
#define KERNEL_END }
