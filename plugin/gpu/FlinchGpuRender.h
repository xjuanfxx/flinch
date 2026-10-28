// FlinchGpuRender.h — turns a FrameState into GPU passes.
//
// Independent of After Effects: the plugin and tools/gpu_check both use it, so
// the GPU path can be compared against the CPU renderer outside AE.
// Mirrors flinch::render_frame (core/include/flinch_render.h).
#pragma once

#include <algorithm>
#include <cmath>

#include "FlinchGpu.h"
#include "flinch_core.h"

namespace flinch::gpu {

// A float4 GPU buffer; pitch in pixels.
struct Buffer {
    void* mem = nullptr;
    int width = 0, height = 0, pitch = 0;
};

// Provides temporary buffers for one render; frees them when destroyed.
class Scratch {
public:
    virtual ~Scratch() = default;
    virtual bool alloc(int width, int height, Buffer& out) = 0;
};

// Where the input and output buffers sit on the whole-layer canvas.
struct Layout {
    int canvas_w = 0, canvas_h = 0;
    int canvas_left = 0, canvas_top = 0;  // canvas corner, in layer coords
    int in_ox = 0, in_oy = 0;             // input buffer corner, in canvas coords
    double out_ox = 0, out_oy = 0;        // output pixel (0,0), in canvas coords
    double pixel_scale = 1.0;             // downsample factor
    int border = 1;                       // flinch::Border value
    bool show_twitch_only = false;
};

namespace detail {

inline void set_segment(FlinchGeoParams& p, int k, const Segment& s, double scale) {
    p.seg[4 * k + 0] = float(s.from.x * scale);
    p.seg[4 * k + 1] = float(s.from.y * scale);
    p.seg[4 * k + 2] = float(s.to.x * scale);
    p.seg[4 * k + 3] = float(s.to.y * scale);
}

inline double segment_length(const Segment& s, double scale) {
    return std::hypot(s.from.x - s.to.x, s.from.y - s.to.y) * scale;
}

}  // namespace detail

inline bool render(Backend& gpu, Scratch& scratch, const FrameState& st, const Layout& l, const Buffer& in,
                   const Buffer& out) {
    const double k = l.pixel_scale;
    const SlideState& sl = st.slide;
    const ScaleState& sc = st.scale;

    FlinchGeoParams g{};
    g.out_w = out.width;
    g.out_h = out.height;
    g.out_pitch = out.pitch;
    g.in_w = in.width;
    g.in_h = in.height;
    g.in_pitch = in.pitch;
    g.in_ox = l.in_ox;
    g.in_oy = l.in_oy;
    g.canvas_w = l.canvas_w;
    g.canvas_h = l.canvas_h;
    g.border = l.border;
    g.out_ox = float(l.out_ox);
    g.out_oy = float(l.out_oy);
    g.samples = 1;
    g.scale_now = g.scale_from = g.scale_to = 1.0f;

    // The common case: no twitch on this frame. Copy (or clear for Show Twitch Only).
    if (!st.any_active()) {
        g.clear = l.show_twitch_only ? 1 : 0;
        return gpu.geometry(in.mem, out.mem, g);
    }

    if (sl.active) {
        detail::set_segment(g, 0, sl.main, k);
        for (int c = 0; c < 3; ++c) detail::set_segment(g, c + 1, sl.rgb_split ? sl.channel[c] : sl.main, k);
        g.split = sl.rgb_split ? 1 : 0;
        g.slide_blur = sl.motion_blur ? 1 : 0;
    }
    double travel = 0.0;
    if (g.slide_blur) {
        travel = detail::segment_length(sl.main, k);
        if (g.split)
            for (int c = 0; c < 3; ++c) travel = std::max(travel, detail::segment_length(sl.channel[c], k));
    }
    if (sc.active) {
        g.origin_x = float(sc.origin.x * k - l.canvas_left);
        g.origin_y = float(sc.origin.y * k - l.canvas_top);
        g.scale_now = float(sc.factor);
        g.scale_from = float(sc.factor_from);
        g.scale_to = float(sc.factor_to);
        g.zoom_blur = sc.motion_blur ? 1 : 0;
        if (g.zoom_blur) {
            const double corner = std::hypot(std::max<double>(g.origin_x, l.canvas_w - g.origin_x),
                                             std::max<double>(g.origin_y, l.canvas_h - g.origin_y));
            travel = std::max(travel, std::abs(sc.factor_to - sc.factor_from) * corner);
        }
    }
    // About one sample per pixel of travel: the GPU affords a smooth trail.
    if (g.slide_blur || g.zoom_blur) g.samples = std::clamp(int(std::ceil(travel)), 2, 256);

    const BlurState& b = st.blur;
    const int rx = b.active ? int(std::lround(b.size_x * k * 0.5)) : 0;
    const int ry = b.active ? int(std::lround(b.size_y * k * 0.5)) : 0;
    const bool blur_on = rx > 0 || ry > 0;
    const bool tone_on = st.light.active || st.color.active;
    if (!blur_on && !tone_on) return gpu.geometry(in.mem, out.mem, g);

    Buffer geo;
    if (!scratch.alloc(out.width, out.height, geo)) return false;
    g.out_pitch = geo.pitch;
    if (!gpu.geometry(in.mem, geo.mem, g)) return false;

    Buffer blurred = geo;
    if (blur_on) {
        FlinchBlurParams bp{};
        bp.w = out.width;
        bp.h = out.height;
        Buffer src = geo;
        for (int pass = 0; pass < 2; ++pass) {
            const int radius = pass == 0 ? rx : ry;
            if (radius <= 0) continue;
            Buffer dst;
            if (!scratch.alloc(out.width, out.height, dst)) return false;
            bp.src_pitch = src.pitch;
            bp.dst_pitch = dst.pitch;
            bp.radius = radius;
            bp.horizontal = pass == 0 ? 1 : 0;
            if (!gpu.blur(src.mem, dst.mem, bp)) return false;
            src = dst;
        }
        blurred = src;
    }

    FlinchFinishParams f{};
    f.w = out.width;
    f.h = out.height;
    f.geo_pitch = geo.pitch;
    f.blur_pitch = blurred.pitch;
    f.out_pitch = out.pitch;
    f.blur_on = blur_on ? 1 : 0;
    f.blur_opacity = float(std::clamp(b.opacity, 0.0, 1.0));
    f.blur_tint_r = float(b.tint.r * b.boost);
    f.blur_tint_g = float(b.tint.g * b.boost);
    f.blur_tint_b = float(b.tint.b * b.boost);
    f.light_gain = st.light.active ? float(st.light.gain) : 1.0f;
    f.color_strength = st.color.active ? float(std::clamp(st.color.strength, 0.0, 1.0)) : 0.0f;
    f.color_r = float(st.color.color.r);
    f.color_g = float(st.color.color.g);
    f.color_b = float(st.color.color.b);
    return gpu.finish(geo.mem, blurred.mem, out.mem, f);
}

}  // namespace flinch::gpu
