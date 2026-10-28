/* flinch_kernels.inc — Flinch GPU kernels, written once for CUDA, OpenCL and Metal.
 *
 * Each backend includes a prelude that defines:
 *   DEVICE        qualifier for helper functions
 *   PRIV          address space of pointers to thread-local data
 *   GBUF_CONST    read-only global float4 buffer, GBUF writable one
 *   F4(x,y,z,w)   float4 constructor
 *   KERNEL1(name, P) / KERNEL2(name, P) ... KERNEL_END
 *       kernel entry with buffers (src, dst) or (src, src2, dst), a parameter
 *       block `p` of type P, and the pixel coordinates `x`, `y`.
 * flinch_gpu_types.h is prepended to this file.
 *
 * Mirrors core/include/flinch_render.h: geometry → blur → light → color.
 */

DEVICE float4 f4_add(float4 a, float4 b) { return F4(a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w); }
DEVICE float4 f4_scale(float4 a, float s) { return F4(a.x * s, a.y * s, a.z * s, a.w * s); }

/* Resolves an index outside [0, n) for the border mode; -1 = transparent. */
DEVICE int wrap_index(int i, int n, int border) {
    if (i >= 0 && i < n) return i;
    if (border == 2) return ((i % n) + n) % n;
    if (border == 1) {
        int period = 2 * n;
        int m = ((i % period) + period) % period;
        return m < n ? m : period - 1 - m;
    }
    if (border == 4) return i < 0 ? 0 : n - 1;
    return -1;
}

/* Pixel of the whole-layer canvas: borders wrap inside the canvas, and areas
 * the input buffer does not cover are transparent. */
DEVICE float4 fetch(GBUF_CONST src, PRIV const FlinchGeoParams* p, int cx, int cy) {
    cx = wrap_index(cx, p->canvas_w, p->border);
    cy = wrap_index(cy, p->canvas_h, p->border);
    if (cx < 0 || cy < 0) return F4(0.0f, 0.0f, 0.0f, 0.0f);
    int lx = cx - p->in_ox;
    int ly = cy - p->in_oy;
    if (lx < 0 || ly < 0 || lx >= p->in_w || ly >= p->in_h) return F4(0.0f, 0.0f, 0.0f, 0.0f);
    return src[ly * p->in_pitch + lx];
}

/* Bilinear sample in canvas coords (center of pixel 0 = 0.5). */
DEVICE float4 sample_canvas(GBUF_CONST src, PRIV const FlinchGeoParams* p, float x, float y) {
    float fx = x - 0.5f, fy = y - 0.5f;
    float x0f = floor(fx), y0f = floor(fy);
    int x0 = (int)x0f, y0 = (int)y0f;
    float tx = fx - x0f, ty = fy - y0f;
    float4 a = fetch(src, p, x0, y0), b = fetch(src, p, x0 + 1, y0);
    float4 c = fetch(src, p, x0, y0 + 1), d = fetch(src, p, x0 + 1, y0 + 1);
    float4 top = f4_add(f4_scale(a, 1.0f - tx), f4_scale(b, tx));
    float4 bottom = f4_add(f4_scale(c, 1.0f - tx), f4_scale(d, tx));
    return f4_add(f4_scale(top, 1.0f - ty), f4_scale(bottom, ty));
}

/* Source pixel for segment `k` at trail position u (0 = time t, 1 = t-1). */
DEVICE float4 sample_segment(GBUF_CONST src, PRIV const FlinchGeoParams* p, float px, float py,
                             int k, float u, float f) {
    float from_x = p->seg[4 * k + 0], from_y = p->seg[4 * k + 1];
    float to_x = p->seg[4 * k + 2], to_y = p->seg[4 * k + 3];
    float w = p->slide_blur ? u : 0.0f;
    float ox = to_x + (from_x - to_x) * w;
    float oy = to_y + (from_y - to_y) * w;
    return sample_canvas(src, p, p->origin_x + (px - ox - p->origin_x) * f,
                         p->origin_y + (py - oy - p->origin_y) * f);
}

DEVICE float4 geometry_pixel(GBUF_CONST src, PRIV const FlinchGeoParams* p, int x, int y) {
    if (p->clear) return F4(0.0f, 0.0f, 0.0f, 0.0f);
    float px = (float)x + 0.5f + p->out_ox;
    float py = (float)y + 0.5f + p->out_oy;
    int n = p->samples < 1 ? 1 : p->samples;
    float4 acc = F4(0.0f, 0.0f, 0.0f, 0.0f);
    for (int s = 0; s < n; ++s) {
        float u = n == 1 ? 0.0f : (float)s / (float)(n - 1);
        float f = p->zoom_blur ? p->scale_from + (p->scale_to - p->scale_from) * u : p->scale_now;
        if (p->split) {
            float4 r = sample_segment(src, p, px, py, 1, u, f);
            float4 g = sample_segment(src, p, px, py, 2, u, f);
            float4 b = sample_segment(src, p, px, py, 3, u, f);
            acc = f4_add(acc, F4(b.x, g.y, r.z, fmax(r.w, fmax(g.w, b.w))));
        } else {
            acc = f4_add(acc, sample_segment(src, p, px, py, 0, u, f));
        }
    }
    return f4_scale(acc, 1.0f / (float)n);
}

KERNEL1(flinch_geometry, FlinchGeoParams)
    if (x >= p.out_w || y >= p.out_h) return;
    dst[y * p.out_pitch + x] = geometry_pixel(src, &p, x, y);
KERNEL_END

KERNEL1(flinch_blur, FlinchBlurParams)
    if (x >= p.w || y >= p.h) return;
    float4 sum = F4(0.0f, 0.0f, 0.0f, 0.0f);
    for (int i = -p.radius; i <= p.radius; ++i) {
        int sx = x, sy = y;
        if (p.horizontal) sx = min(max(x + i, 0), p.w - 1);
        else sy = min(max(y + i, 0), p.h - 1);
        sum = f4_add(sum, src[sy * p.src_pitch + sx]);
    }
    dst[y * p.dst_pitch + x] = f4_scale(sum, 1.0f / (float)(2 * p.radius + 1));
KERNEL_END

/* src = geometry result, src2 = blurred copy (unused when blur is off). */
KERNEL2(flinch_finish, FlinchFinishParams)
    if (x >= p.w || y >= p.h) return;
    float4 o = src[y * p.geo_pitch + x];
    if (p.blur_on) {
        float4 q = src2[y * p.blur_pitch + x];
        float op = p.blur_opacity;
        o = F4(o.x + (q.x * p.blur_tint_b - o.x) * op,
               o.y + (q.y * p.blur_tint_g - o.y) * op,
               o.z + (q.z * p.blur_tint_r - o.z) * op,
               o.w + (q.w - o.w) * op);
    }
    o = F4(o.x * p.light_gain, o.y * p.light_gain, o.z * p.light_gain, o.w);
    if (p.color_strength > 0.0f) {
        float s = p.color_strength;
        float luma = 0.2126f * o.z + 0.7152f * o.y + 0.0722f * o.x;
        o = F4(o.x + (luma * p.color_b - o.x) * s,
               o.y + (luma * p.color_g - o.y) * s,
               o.z + (luma * p.color_r - o.z) * s,
               o.w);
    }
    dst[y * p.out_pitch + x] = o;
KERNEL_END
