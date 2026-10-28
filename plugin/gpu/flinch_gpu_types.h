/* flinch_gpu_types.h — parameter blocks shared by the host and the GPU kernels.
 *
 * Compiled as C++ on the host and embedded verbatim in the CUDA, OpenCL and
 * Metal kernel sources, so it must stay plain C: only `int`/`float` fields
 * (4 bytes each, no padding), no includes, no host-only types.
 *
 * Pixels are float4 in After Effects' GPU layout: x = B, y = G, z = R, w = A,
 * premultiplied. Pitches are in pixels.
 */
#ifndef FLINCH_GPU_TYPES_H
#define FLINCH_GPU_TYPES_H

/* Geometry pass: Scale (zoom) + Slide (offset, RGB split, motion trail). */
typedef struct FlinchGeoParams {
    int out_w, out_h, out_pitch;
    int in_w, in_h, in_pitch;
    int in_ox, in_oy;          /* input buffer corner, in canvas coords */
    int canvas_w, canvas_h;    /* whole layer; borders wrap inside it */
    int border;                /* 1 mirror, 2 tile, 3 transparent, 4 clamp */
    int clear;                 /* write transparent pixels (Show Twitch Only) */
    int samples;               /* motion blur samples along the trail (>= 1) */
    int split;                 /* sample R, G and B with their own segments */
    int zoom_blur;             /* blend zoom factors from scale_from to scale_to */
    int slide_blur;            /* blend offsets from segment `to` to `from` */
    float out_ox, out_oy;      /* canvas position of output pixel (0,0) */
    float origin_x, origin_y;  /* Scale origin, canvas coords */
    float scale_now, scale_from, scale_to;
    /* Segments as {from.x, from.y, to.x, to.y}: [0] main, [1..3] R, G, B. */
    float seg[16];
} FlinchGeoParams;

/* Box blur along one axis, edges clamped. */
typedef struct FlinchBlurParams {
    int w, h, src_pitch, dst_pitch;
    int radius;
    int horizontal;
} FlinchBlurParams;

/* Final pass: blend the blur, then Light gain and Color. */
typedef struct FlinchFinishParams {
    int w, h, geo_pitch, blur_pitch, out_pitch;
    int blur_on;
    float blur_opacity;
    float blur_tint_r, blur_tint_g, blur_tint_b;  /* tint × boost */
    float light_gain;
    float color_strength;
    float color_r, color_g, color_b;
} FlinchFinishParams;

#endif
