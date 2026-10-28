// flinch_render.h — applies a FrameState to a premultiplied float RGBA image.
//
// Independent of After Effects: the plugin converts its 8/16/32-bit buffers to
// `Image`, calls `render_frame` and converts back, so rendering can be tested
// without AE.
#pragma once

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <thread>
#include <vector>

#include "flinch_core.h"

namespace flinch {

struct Pixel {
    float r = 0, g = 0, b = 0, a = 0;
};

struct Image {
    int width = 0, height = 0;
    std::vector<Pixel> px;

    Image() = default;
    Image(int w, int h) : width(w), height(h), px(size_t(w) * size_t(h)) {}
    Pixel& at(int x, int y) { return px[size_t(y) * size_t(width) + size_t(x)]; }
    const Pixel& at(int x, int y) const { return px[size_t(y) * size_t(width) + size_t(x)]; }
};

enum class Border { Mirror = 1, Tile = 2, Transparent = 3, Clamp = 4 };

// Parallel passes running across the whole process (AE renders several frames at once).
inline std::atomic<int>& active_passes() {
    static std::atomic<int> count{0};
    return count;
}

// Splits rows [0, rows) across threads in contiguous blocks (each thread owns its range).
// Cores are shared among concurrent passes so the CPU is not oversubscribed.
template <class Fn>
void parallel_rows(int rows, Fn&& fn) {
    struct Guard {
        int concurrent = ++active_passes();
        ~Guard() { --active_passes(); }
    } guard;
    const int cores = std::max(1, int(std::thread::hardware_concurrency()));
    const int share = std::max(1, cores / std::max(1, guard.concurrent));
    const int workers = std::max(1, std::min(rows / 16, share));
    if (workers <= 1) {
        for (int y = 0; y < rows; ++y) fn(y);
        return;
    }
    std::vector<std::thread> pool;
    pool.reserve(size_t(workers));
    for (int w = 0; w < workers; ++w) {
        pool.emplace_back([&, w] {
            const int begin = int(int64_t(rows) * w / workers);
            const int end = int(int64_t(rows) * (w + 1) / workers);
            for (int y = begin; y < end; ++y) fn(y);
        });
    }
    for (auto& t : pool) t.join();
}

namespace detail {

// Resolves an out-of-range index for the border mode; -1 = transparent.
inline int wrap(int i, int n, Border border) {
    if (i >= 0 && i < n) return i;
    switch (border) {
        case Border::Tile: return ((i % n) + n) % n;
        case Border::Mirror: {
            const int period = 2 * n;
            int m = ((i % period) + period) % period;
            return m < n ? m : period - 1 - m;
        }
        case Border::Clamp: return std::clamp(i, 0, n - 1);
        default: return -1;
    }
}

inline Pixel fetch(const Image& img, int x, int y, Border border) {
    const int xi = wrap(x, img.width, border);
    const int yi = wrap(y, img.height, border);
    if (xi < 0 || yi < 0) return {};
    return img.at(xi, yi);
}

}  // namespace detail

// Bilinear sampling in pixel coordinates (center of pixel 0 = 0.5).
inline Pixel sample(const Image& img, double x, double y, Border border) {
    const double fx = x - 0.5, fy = y - 0.5;
    const int x0 = int(std::floor(fx)), y0 = int(std::floor(fy));
    const float tx = float(fx - x0), ty = float(fy - y0);
    auto mix = [&](const Pixel& p00, const Pixel& p10, const Pixel& p01, const Pixel& p11) {
        const float w00 = (1 - tx) * (1 - ty), w10 = tx * (1 - ty), w01 = (1 - tx) * ty, w11 = tx * ty;
        return Pixel{p00.r * w00 + p10.r * w10 + p01.r * w01 + p11.r * w11,
                     p00.g * w00 + p10.g * w10 + p01.g * w01 + p11.g * w11,
                     p00.b * w00 + p10.b * w10 + p01.b * w01 + p11.b * w11,
                     p00.a * w00 + p10.a * w10 + p01.a * w01 + p11.a * w11};
    };
    // Common case: all 4 neighbours inside the image, no border logic.
    if (x0 >= 0 && y0 >= 0 && x0 + 1 < img.width && y0 + 1 < img.height) {
        const Pixel* row0 = &img.at(x0, y0);
        const Pixel* row1 = row0 + img.width;
        return mix(row0[0], row0[1], row1[0], row1[1]);
    }
    return mix(detail::fetch(img, x0, y0, border), detail::fetch(img, x0 + 1, y0, border),
               detail::fetch(img, x0, y0 + 1, border), detail::fetch(img, x0 + 1, y0 + 1, border));
}

// How output buffer coordinates map onto the source.
struct Placement {
    double src_x = 0, src_y = 0;  // position of output (0,0) in source coords
    double pixel_scale = 1.0;     // downsample factor (1 = full resolution)
};

namespace detail {

inline void add_scaled(Pixel& acc, const Pixel& p, float w) {
    acc.r += p.r * w; acc.g += p.g * w; acc.b += p.b * w; acc.a += p.a * w;
}

// out(x,y) = mean of A(x − s·dx, y − s·dy) for s ∈ [−½, ½]: a box blur along a
// segment, with constant cost per pixel regardless of its length.
//
// Walks lines parallel to the segment (rows "j" with slope k over the major axis),
// averages them with a sliding window and splits each result between the two
// neighbouring output rows. A(x,y) takes output coords (center = integer).
template <class Sampler>
void line_box(const Sampler& A, Image& out, double dx, double dy) {
    const bool transposed = std::abs(dy) > std::abs(dx);
    const int W = transposed ? out.height : out.width;  // major axis (u)
    const int H = transposed ? out.width : out.height;  // minor axis (v)
    const double du = transposed ? dy : dx, dv = transposed ? dx : dy;
    const double k = dv / du;  // |k| <= 1
    const double r = std::abs(du) * 0.5;
    const int ri = int(r), R = ri + 1;
    const float fr = float(r - ri);  // weight of the end samples (fractional radius)
    const float norm = 1.0f / (2.0f * ri + 1.0f + 2.0f * fr);
    auto at = [&](double u, double v) { return transposed ? A(v, u) : A(u, v); };
    auto put = [&](int u, int v) -> Pixel& { return transposed ? out.at(v, u) : out.at(u, v); };

    parallel_rows(out.height, [&](int y) {
        std::fill_n(&out.at(0, y), size_t(out.width), Pixel{});
    });
    // Rows j covering v − k·u for every output pixel.
    const double reach = k * (W - 1);
    const int j0 = int(std::floor(-std::max(0.0, reach))) - 1;
    const int j1 = int(std::floor(H - 1 - std::min(0.0, reach))) + 1;
    const int rows = j1 - j0 + 1;
    // Even and odd rows separately: each pixel gets one of each, so no data races.
    for (int phase = 0; phase < 2; ++phase) {
        parallel_rows((rows - phase + 1) / 2, [&](int idx) {
            const int j = j0 + phase + 2 * idx;
            thread_local std::vector<Pixel> line;
            line.resize(size_t(W + 2 * R));
            for (int i = 0; i < W + 2 * R; ++i) {
                const int u = i - R;
                line[size_t(i)] = at(u, j + k * u);
            }
            Pixel sum;
            for (int i = -ri; i <= ri; ++i) add_scaled(sum, line[size_t(R + i)], 1.0f);
            for (int u = 0; u < W; ++u) {
                Pixel h = sum;
                add_scaled(h, line[size_t(R + u - ri - 1)], fr);
                add_scaled(h, line[size_t(R + u + ri + 1)], fr);
                const double vc = j + k * u;
                const int v0 = int(std::floor(vc));
                const float w1 = float(vc - v0);
                if (v0 >= 0 && v0 < H) add_scaled(put(u, v0), h, (1.0f - w1) * norm);
                if (w1 > 0.0f && v0 + 1 >= 0 && v0 + 1 < H) add_scaled(put(u, v0 + 1), h, w1 * norm);
                add_scaled(sum, line[size_t(R + u + ri + 1)], 1.0f);
                add_scaled(sum, line[size_t(R + u - ri)], -1.0f);
            }
        });
    }
}

// out(x,y) = mean of src at scale factors {f·a, f/a} around origin, evaluated at (x+ox, y+oy).
// With a = 1 it is a plain zoom.
inline void zoom_pass(const Image& src, Image& out, double ox, double oy, const Vec2& origin,
                      double f, double a, Border border) {
    parallel_rows(out.height, [&](int y) {
        for (int x = 0; x < out.width; ++x) {
            const double dx = x + 0.5 + ox - origin.x, dy = y + 0.5 + oy - origin.y;
            if (a == 1.0) {
                out.at(x, y) = sample(src, origin.x + dx * f, origin.y + dy * f, border);
                continue;
            }
            Pixel acc;
            add_scaled(acc, sample(src, origin.x + dx * f * a, origin.y + dy * f * a, border), 0.5f);
            add_scaled(acc, sample(src, origin.x + dx * f / a, origin.y + dy * f / a, border), 0.5f);
            out.at(x, y) = acc;
        }
    });
}

// Zoom around `origin`. With motion blur it averages the factors between from and to
// (geometric spacing) by chaining 2-sample passes: n passes = 2^n factors.
inline void zoom(const Image& src, Image& out, double ox, double oy, const ScaleState& sc,
                 const Vec2& origin, Border border) {
    const double fa = std::min(sc.factor_from, sc.factor_to), fb = std::max(sc.factor_from, sc.factor_to);
    if (!sc.motion_blur || fb / fa < 1.0005) {
        zoom_pass(src, out, ox, oy, origin, sc.factor, 1.0, border);
        return;
    }
    const double corner = std::hypot(std::max(origin.x, src.width - origin.x),
                                     std::max(origin.y, src.height - origin.y));
    const int taps = std::clamp(int(std::ceil((fb - fa) * corner / 1.5)), 2, 64);
    const int passes = int(std::ceil(std::log2(double(taps))));
    const double log_q = std::log(fb / fa);

    Image a(src.width, src.height), b(src.width, src.height);
    zoom_pass(src, a, 0, 0, origin, std::sqrt(fa * fb), 1.0, border);
    for (int i = 0; i < passes; ++i) {
        const double step = std::exp(log_q / double(int64_t(4) << i));  // q^(1/2^(i+2))
        if (i + 1 == passes) {
            zoom_pass(a, out, ox, oy, origin, 1.0, step, border);
        } else {
            zoom_pass(a, b, 0, 0, origin, 1.0, step, border);
            std::swap(a, b);
        }
    }
}

// Geometry pass: Scale (zoom) then Slide (displacement + RGB split + trail).
inline void geometry(const Image& src, Image& dst, const FrameState& st, const Placement& pl,
                     Border border) {
    const double k = pl.pixel_scale;
    const ScaleState& sc = st.scale;
    const SlideState& sl = st.slide;
    const Vec2 origin{sc.origin.x * k, sc.origin.y * k};

    if (sc.active && !sl.active) {
        zoom(src, dst, pl.src_x, pl.src_y, sc, origin, border);
        return;
    }
    // Zoom with motion blur gets its own pass; without it, the zoom is folded into Slide sampling.
    Image zoomed;
    const Image* base = &src;
    double f = 1.0;
    if (sc.active && sc.motion_blur) {
        zoomed = Image(src.width, src.height);
        zoom(src, zoomed, 0, 0, sc, origin, border);
        base = &zoomed;
    } else if (sc.active) {
        f = sc.factor;
    }

    // Fills `out` with `base` displaced by segment g (with a trail when motion blur is on).
    auto slide_into = [&](const Segment& g, Image& out) {
        const Vec2 to{g.to.x * k, g.to.y * k}, from{g.from.x * k, g.from.y * k};
        const double dx = from.x - to.x, dy = from.y - to.y;
        const bool trail = sl.motion_blur && std::hypot(dx, dy) >= 0.5;
        const Vec2 off = trail ? Vec2{(to.x + from.x) * 0.5, (to.y + from.y) * 0.5} : to;
        auto A = [&](double x, double y) {
            const double px = x + 0.5 + pl.src_x - off.x, py = y + 0.5 + pl.src_y - off.y;
            return sample(*base, origin.x + (px - origin.x) * f, origin.y + (py - origin.y) * f, border);
        };
        if (trail) {
            line_box(A, out, dx, dy);
        } else {
            parallel_rows(out.height, [&](int y) {
                for (int x = 0; x < out.width; ++x) out.at(x, y) = A(x, y);
            });
        }
    };

    if (!sl.rgb_split) {
        slide_into(sl.active ? sl.main : Segment{}, dst);
        return;
    }
    // Moderate RGB split: the channel trails nearly match the main one.
    // Compute the trail once and only shift each channel into place (≈3× faster).
    if (sl.split_amount <= 0.5) {
        Image trail(dst.width, dst.height);
        slide_into(sl.main, trail);
        auto mid = [&](const Segment& g) {
            const double w = sl.motion_blur ? 0.5 : 0.0;
            return Vec2{(g.to.x + (g.from.x - g.to.x) * w) * k, (g.to.y + (g.from.y - g.to.y) * w) * k};
        };
        const Vec2 m = mid(sl.main);
        Vec2 shift[3];
        for (int c = 0; c < 3; ++c) {
            const Vec2 mc = mid(sl.channel[c]);
            shift[c] = {mc.x - m.x, mc.y - m.y};
        }
        parallel_rows(dst.height, [&](int y) {
            for (int x = 0; x < dst.width; ++x) {
                const Pixel r = sample(trail, x + 0.5 - shift[0].x, y + 0.5 - shift[0].y, Border::Clamp);
                const Pixel g = sample(trail, x + 0.5 - shift[1].x, y + 0.5 - shift[1].y, Border::Clamp);
                const Pixel b = sample(trail, x + 0.5 - shift[2].x, y + 0.5 - shift[2].y, Border::Clamp);
                dst.at(x, y) = {r.r, g.g, b.b, std::max({r.a, g.a, b.a})};
            }
        });
        return;
    }

    Image channel(dst.width, dst.height);
    for (int c = 0; c < 3; ++c) {
        slide_into(sl.channel[c], channel);
        parallel_rows(dst.height, [&](int y) {
            for (int x = 0; x < dst.width; ++x) {
                const Pixel& p = channel.at(x, y);
                Pixel& o = dst.at(x, y);
                if (c == 0) o = {p.r, 0.0f, 0.0f, p.a};
                else if (c == 1) { o.g = p.g; o.a = std::max(o.a, p.a); }
                else { o.b = p.b; o.a = std::max(o.a, p.a); }
            }
        });
    }
}

// Box blur of `radius` along one axis (running sums).
inline void box_blur_axis(const Image& src, Image& dst, int radius, bool horizontal) {
    const int len = horizontal ? src.width : src.height;
    const int lines = horizontal ? src.height : src.width;
    const float norm = 1.0f / float(2 * radius + 1);
    parallel_rows(lines, [&](int line) {
        auto get = [&](int i) {
            i = std::clamp(i, 0, len - 1);
            return horizontal ? src.at(i, line) : src.at(line, i);
        };
        Pixel sum;
        for (int i = -radius; i <= radius; ++i) {
            const Pixel p = get(i);
            sum.r += p.r; sum.g += p.g; sum.b += p.b; sum.a += p.a;
        }
        for (int i = 0; i < len; ++i) {
            Pixel& out = horizontal ? dst.at(i, line) : dst.at(line, i);
            out = {sum.r * norm, sum.g * norm, sum.b * norm, sum.a * norm};
            const Pixel add = get(i + radius + 1), sub = get(i - radius);
            sum.r += add.r - sub.r; sum.g += add.g - sub.g;
            sum.b += add.b - sub.b; sum.a += add.a - sub.a;
        }
    });
}

inline void blur(Image& img, const BlurState& b, double pixel_scale) {
    const int rx = int(std::lround(b.size_x * pixel_scale * 0.5));
    const int ry = int(std::lround(b.size_y * pixel_scale * 0.5));
    if (rx <= 0 && ry <= 0) return;
    Image tmp(img.width, img.height), blurred = img;
    if (rx > 0) { box_blur_axis(blurred, tmp, rx, true); std::swap(blurred, tmp); }
    if (ry > 0) { box_blur_axis(blurred, tmp, ry, false); std::swap(blurred, tmp); }

    const float op = float(std::clamp(b.opacity, 0.0, 1.0));
    const float tr = float(b.tint.r * b.boost), tg = float(b.tint.g * b.boost),
                tb = float(b.tint.b * b.boost);
    parallel_rows(img.height, [&](int y) {
        for (int x = 0; x < img.width; ++x) {
            Pixel& o = img.at(x, y);
            const Pixel& q = blurred.at(x, y);
            o.r += (q.r * tr - o.r) * op;
            o.g += (q.g * tg - o.g) * op;
            o.b += (q.b * tb - o.b) * op;
            o.a += (q.a - o.a) * op;
        }
    });
}

inline void tone(Image& img, const LightState& light, const ColorState& color) {
    const float gain = light.active ? float(light.gain) : 1.0f;
    const float s = color.active ? float(std::clamp(color.strength, 0.0, 1.0)) : 0.0f;
    const float cr = float(color.color.r), cg = float(color.color.g), cb = float(color.color.b);
    parallel_rows(img.height, [&](int y) {
        for (int x = 0; x < img.width; ++x) {
            Pixel& p = img.at(x, y);
            p.r *= gain; p.g *= gain; p.b *= gain;
            if (s > 0.0f) {
                const float luma = 0.2126f * p.r + 0.7152f * p.g + 0.0722f * p.b;
                p.r += (luma * cr - p.r) * s;
                p.g += (luma * cg - p.g) * s;
                p.b += (luma * cb - p.b) * s;
            }
        }
    });
}

}  // namespace detail

// Full render. `src` is the layer (already at the time Time asks for); `dst` must
// be pre-sized. Order: geometry → blur → light → color.
inline void render_frame(const Image& src, Image& dst, const FrameState& st, const Placement& pl,
                         Border border) {
    detail::geometry(src, dst, st, pl, border);
    if (st.blur.active) detail::blur(dst, st.blur, pl.pixel_scale);
    if (st.light.active || st.color.active) detail::tone(dst, st.light, st.color);
}

}  // namespace flinch
