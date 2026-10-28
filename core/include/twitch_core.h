// twitch_core.h — timing and randomness core of Twitch Rework.
//
// New implementation written from analysis/twitch-operators-spec.md.
// Independent of After Effects: takes parameters and a frame number and
// returns the state of every operator. Rendering (blur, displacement, etc.)
// lives elsewhere and only consumes FrameState.
//
// Header-only, C++17, no global state: safe to call from multiple threads.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace twr {

constexpr double kPi = 3.14159265358979323846;

// ---------------------------------------------------------------------------
// Deterministic randomness
// ---------------------------------------------------------------------------

enum class RandomMode {
    // Stateless hash (splitmix64). O(1), recommended.
    Stateless,
    // Reproduces Twitch's pattern: the MSVC CRT LCG seeded with the key and
    // advanced |slot|+5 steps. O(slot); only for comparing against Twitch.
    Legacy,
};

// Deterministic value in [0,1] for (slot, key).
inline double random01(int64_t slot, uint32_t key, RandomMode mode) {
    if (mode == RandomMode::Legacy) {
        const int64_t s = std::max<int64_t>(slot, -4);
        const int64_t steps = (s < 0 ? -s : s) + 5;
        uint32_t state = key;
        for (int64_t k = 0; k < steps; ++k) state = state * 214013u + 2531011u;
        state = state * 214013u + 2531011u;
        return double((state >> 16) & 0x7FFFu) / 32767.0;
    }
    uint64_t z = uint64_t(slot) * 0x9E3779B97F4A7C15ull ^ (uint64_t(key) << 32 | key);
    z += 0x9E3779B97F4A7C15ull;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    z ^= z >> 31;
    return double(z >> 11) * (1.0 / 9007199254740991.0);  // 53 bits → [0,1]
}

// Offset of each random stream (spec §3).
namespace stream {
constexpr uint32_t kEvents = 0;
constexpr uint32_t kSlideSign = 8000;
constexpr uint32_t kSlideAngle = 19000;      // +0 main, +1000·(1+channel)
constexpr uint32_t kSlideChannelAmp = 23000;  // +1000·channel
constexpr uint32_t kBlurAmp = 9000;
constexpr uint32_t kTimeAmp = 10000;
constexpr uint32_t kTimeDir = 10001;
constexpr uint32_t kLightAmp = 11000;
constexpr uint32_t kColorAmp = 12000;
constexpr uint32_t kColorJitter = 16000;  // +1000·channel
constexpr uint32_t kScaleAmp = 13000;
constexpr uint32_t kScaleOrigin = 26000;  // +0 X, +1000 Y
}  // namespace stream

// ---------------------------------------------------------------------------
// Event scheduling and envelope (spec §3–§5)
// ---------------------------------------------------------------------------

// Frames per slot; 0 if the operator never fires.
inline int slot_length(double fps, double twitches_per_sec, double speed) {
    const double rate = twitches_per_sec * speed;
    if (rate <= 0.0 || fps <= 0.0) return 0;
    return std::max(1, int(fps / rate));
}

inline int64_t slot_of(double t, int n) { return int64_t(std::floor(t / n)); }

struct Events {
    double prev;  // last event <= t
    double next;  // first event > t
};

inline double event_time(int64_t slot, int n, uint32_t seed, RandomMode mode) {
    return (double(slot) + random01(slot, seed + stream::kEvents, mode)) * n;
}

inline Events events_around(double t, int n, uint32_t seed, RandomMode mode) {
    const int64_t i = slot_of(t, n);
    const double here = event_time(i, n, seed, mode);
    if (here <= t) return {here, event_time(i + 1, n, seed, mode)};
    return {event_time(i - 1, n, seed, mode), here};
}

// Decay (after the previous event) and attack (before the next one) contributions.
struct Ramps {
    double out;
    double in;
};

inline Ramps ramps(double t, const Events& e, double ease_in, double ease_out) {
    return {1.0 - (t - e.prev) / (ease_out + 1.0), 1.0 - (e.next - t) / (ease_in + 1.0)};
}

// Linear triangle; overlapping twitches combine with max.
inline double envelope(double t, int n, uint32_t seed, double ease_in, double ease_out,
                       RandomMode mode) {
    const Ramps r = ramps(t, events_around(t, n, seed, mode), ease_in, ease_out);
    return std::max(0.0, std::max(r.out, r.in));
}

// Per-slot random value in [minimum, 1].
inline double random_range(double t, int n, uint32_t key, double minimum, RandomMode mode) {
    const double u = random01(slot_of(t, n), key, mode);
    return minimum + (1.0 - minimum) * u;
}

// Random value in [0,1] pushed away from 0.5 by `minimum` (Slide sign).
inline double random_polar(double t, int n, uint32_t key, double minimum, RandomMode mode) {
    const double u = random01(slot_of(t, n), key, mode);
    const double m = std::abs(u - 0.5) * 2.0;
    const double half = ((1.0 - minimum) * m + minimum) * 0.5;
    return std::clamp(0.5 + (u - 0.5 <= 0.0 ? -half : half), 0.0, 1.0);
}

// Envelopes at t-1 and t, adjusted so motion blur covers the whole travel:
// if a peak is crossed between both samples the high end becomes 1; if the
// valley between two twitches is crossed the low end becomes 0.
struct EnvelopePair {
    double now;
    double prev;
};

inline EnvelopePair envelope_pair(double t, int n, uint32_t seed, double ease_in,
                                  double ease_out, RandomMode mode) {
    const double t0 = t - 1.0;
    const Events e0 = events_around(t0, n, seed, mode);
    const Events e1 = events_around(t, n, seed, mode);
    const Ramps r0 = ramps(t0, e0, ease_in, ease_out);
    const Ramps r1 = ramps(t, e1, ease_in, ease_out);
    EnvelopePair p{std::max(0.0, std::max(r1.out, r1.in)), std::max(0.0, std::max(r0.out, r0.in))};

    const bool crossed_peak = e0.prev != e1.prev;
    bool crossed_valley = false;
    if (!crossed_peak) {
        // Same pair of events: did the dominant ramp change between t-1 and t?
        const Ramps s1 = ramps(t, e0, ease_in, ease_out);
        crossed_valley = (r0.in < r0.out && s1.out < s1.in) || (r0.out < r0.in && s1.in < s1.out);
    }
    double& low = p.now <= p.prev ? p.now : p.prev;
    double& high = p.now <= p.prev ? p.prev : p.now;
    if (crossed_valley) low = 0.0;
    if (crossed_peak) high = 1.0;
    return p;
}

// ---------------------------------------------------------------------------
// Parameters
// ---------------------------------------------------------------------------

struct Vec2 {
    double x = 0.0, y = 0.0;
};
struct Rgb {
    double r = 1.0, g = 1.0, b = 1.0;
};

struct OperatorParams {
    bool enabled = true;
    double amount = 50.0;           // %
    double twitches_per_sec = 1.0;  // multiplied by Params::speed
    int unique_seed = 0;
};

enum class LightMode { Brighter = 1, Darker = 2, Both = 3 };
enum class TimeDirection { Both = 1, ForwardOnly = 2, BackwardOnly = 3 };

// Values as the user sees them (percentages in 0–100 unless noted).
struct Params {
    double amount = 100.0;
    double speed = 5.0;
    double ease_in = 1.0;   // frames
    double ease_out = 1.0;  // frames
    double randomize_min = 0.0;
    int seed = 0;

    OperatorParams blur, color, light, scale, slide, time;

    double blur_aspect = 0.0;  // -1 (horizontal) … 1 (vertical)
    double blur_boost = 100.0;
    double blur_opacity = 100.0;
    Rgb blur_tint{1.0, 1.0, 1.0};

    Rgb colorize{1.0, 0.635, 0.0};
    double color_randomize = 0.0;

    LightMode light_mode = LightMode::Brighter;

    Vec2 scale_origin;  // layer pixels
    double scale_origin_randomize = 0.0;
    double scale_motion_blur = 0.0;

    double slide_direction_deg = 0.0;
    double slide_spread = 1.0;    // 0…1
    double slide_tendency = 0.0;  // -1…1
    double slide_rgb_split = 0.0;
    double slide_motion_blur = 100.0;

    TimeDirection time_direction = TimeDirection::Both;
};

// ---------------------------------------------------------------------------
// Per-frame state
// ---------------------------------------------------------------------------

struct BlurState {
    bool active = false;
    double envelope = 0.0;
    double size_x = 0.0, size_y = 0.0;  // pixels
    double mix = 0.0;                   // 0…1, drives tint/opacity/boost
    double boost = 1.0;
    double opacity = 0.0;
    Rgb tint;
};

struct ColorState {
    bool active = false;
    double envelope = 0.0;
    double strength = 0.0;  // 0…1+
    Rgb color;
};

struct LightState {
    bool active = false;
    double envelope = 0.0;
    double gain = 1.0;
};

struct ScaleState {
    bool active = false;
    double envelope = 0.0;
    double factor = 1.0;  // sampling factor at t (<1 = zoom in)
    double factor_from = 1.0, factor_to = 1.0;  // motion blur range
    bool motion_blur = false;
    Vec2 origin;
};

// Displacement in pixels: `to` is the position at t; `from` is the start of the
// motion blur trail (equal to `to` when there is no blur).
struct Segment {
    Vec2 from, to;
};

struct SlideState {
    bool active = false;
    double envelope = 0.0;
    Segment main;
    Segment channel[3];  // R, G, B (with RGB split)
    bool rgb_split = false;
    double split_amount = 0.0;  // RGB Split 0…1
    bool motion_blur = false;
};

struct TimeState {
    bool active = false;
    double envelope = 0.0;
    int frame_offset = 0;
};

struct FrameState {
    BlurState blur;
    ColorState color;
    LightState light;
    ScaleState scale;
    SlideState slide;
    TimeState time;

    // Do pixels need processing? (Time only changes which frame is read.)
    bool any_active() const {
        return blur.active || color.active || light.active || scale.active || slide.active;
    }
};

// ---------------------------------------------------------------------------
// Evaluation
// ---------------------------------------------------------------------------

namespace detail {

struct Ctx {
    double t;  // frame center
    double fps;
    double width, height;
    double gain, gain_prev;  // global Amount / 100, at t and t-1
    double rmin, rmin_prev;  // Randomize Minimum / 100, at t and t-1
    double ease_in, ease_out;
    RandomMode mode;
};

inline uint32_t op_seed(const Params& p, const OperatorParams& op) {
    return uint32_t(op.unique_seed + p.seed);
}

inline int op_slot(const Params& p, const OperatorParams& op, double fps) {
    if (!op.enabled || p.speed <= 0.0) return 0;
    return slot_length(fps, op.twitches_per_sec, p.speed);
}

// Angle in AE compass convention (0 = up, clockwise; Y points down).
// A positive magnitude moves toward the angle, in % of the layer size.
inline Vec2 polar_offset(double magnitude, double angle, double w, double h) {
    return {std::sin(angle) * magnitude * w * 0.01, -std::cos(angle) * magnitude * h * 0.01};
}

inline Vec2 lerp(const Vec2& a, const Vec2& b, double k) {
    return {a.x + (b.x - a.x) * k, a.y + (b.y - a.y) * k};
}

inline BlurState blur(const Params& p, const Ctx& c) {
    BlurState s;
    const int n = op_slot(p, p.blur, c.fps);
    if (!n) return s;
    const uint32_t seed = op_seed(p, p.blur);
    s.envelope = envelope(c.t, n, seed, c.ease_in, c.ease_out, c.mode);
    const double amt = c.gain * p.blur.amount * s.envelope *
                       random_range(c.t, n, seed + stream::kBlurAmp, c.rmin, c.mode);
    s.mix = std::min(1.0, amt * 0.1);
    s.boost = 1.0 + (p.blur_boost * 0.01 - 1.0) * s.mix;
    s.opacity = s.mix * p.blur_opacity * 0.01;
    s.tint = {1.0 + (p.blur_tint.r - 1.0) * s.mix, 1.0 + (p.blur_tint.g - 1.0) * s.mix,
              1.0 + (p.blur_tint.b - 1.0) * s.mix};
    const double angle = (p.blur_aspect + 1.0) * 0.5 * (kPi / 2.0);
    s.size_x = std::cos(angle) * std::sqrt(2.0) * amt;
    s.size_y = std::sin(angle) * std::sqrt(2.0) * amt;
    s.active = s.size_x != 0.0 || s.size_y != 0.0;
    return s;
}

inline ColorState color(const Params& p, const Ctx& c) {
    ColorState s;
    const int n = op_slot(p, p.color, c.fps);
    if (!n) return s;
    const uint32_t seed = op_seed(p, p.color);
    s.envelope = envelope(c.t, n, seed, c.ease_in, c.ease_out, c.mode);
    s.strength = c.gain * p.color.amount * s.envelope *
                 random_range(c.t, n, seed + stream::kColorAmp, c.rmin, c.mode) * 0.01;
    const double spread = p.color_randomize * 0.01;
    auto jitter = [&](int ch) {
        const uint32_t key = seed + stream::kColorJitter + 1000u * ch;
        return spread * (random_range(c.t, n, key, 0.0, c.mode) * 2.0 - 1.0);
    };
    s.color = {std::clamp(p.colorize.r + jitter(0), 0.0, 1.0),
               std::clamp(p.colorize.g + jitter(1), 0.0, 1.0),
               std::clamp(p.colorize.b + jitter(2), 0.0, 1.0)};
    s.active = s.strength != 0.0;
    return s;
}

inline LightState light(const Params& p, const Ctx& c) {
    LightState s;
    const int n = op_slot(p, p.light, c.fps);
    if (!n) return s;
    const uint32_t seed = op_seed(p, p.light);
    s.envelope = envelope(c.t, n, seed, c.ease_in, c.ease_out, c.mode);
    const double rnd = random_range(c.t, n, seed + stream::kLightAmp, c.rmin, c.mode);
    const double amt = c.gain * p.light.amount * s.envelope * rnd * 0.03;
    bool darker = p.light_mode == LightMode::Darker;
    if (p.light_mode == LightMode::Both) darker = (int(rnd * 100000.0) & 1) != 0;
    s.gain = darker ? 1.0 / (1.0 + amt) : 1.0 + amt;
    s.active = s.gain != 1.0;
    return s;
}

inline ScaleState scale(const Params& now, const Params& prev, const Ctx& c) {
    ScaleState s;
    const int n = op_slot(now, now.scale, c.fps);
    if (!n) return s;
    const uint32_t seed = op_seed(now, now.scale);
    const EnvelopePair env = envelope_pair(c.t, n, seed, c.ease_in, c.ease_out, c.mode);
    s.envelope = env.now;

    const uint32_t amp_key = seed + stream::kScaleAmp;
    const double amt = c.gain * now.scale.amount * random_range(c.t, n, amp_key, c.rmin, c.mode);
    const double amt_prev = c.gain_prev * prev.scale.amount *
                            random_range(c.t - 1.0, n, amp_key, c.rmin_prev, c.mode);
    const double f_now = std::max(0.25, 1.0 / (env.now * amt * 0.01 + 1.0));
    const double f_prev = std::max(0.25, 1.0 / (env.prev * amt_prev * 0.01 + 1.0));

    const double jitter = now.scale_origin_randomize * 0.01;
    const double jx = (random_range(c.t, n, seed + stream::kScaleOrigin, 0.0, c.mode) - 0.5) * jitter;
    const double jy =
        (random_range(c.t, n, seed + stream::kScaleOrigin + 1000u, 0.0, c.mode) - 0.5) * jitter;
    s.origin = {now.scale_origin.x + jx * c.width, now.scale_origin.y + jy * c.height};

    const double mb = now.scale_motion_blur * 0.01;
    const double f_trail = (1.0 - mb) * f_now + mb * f_prev;
    s.factor = s.factor_from = s.factor_to = f_now;
    if (mb > 0.0 && f_trail != f_now) {
        s.motion_blur = true;
        s.factor_from = std::min(f_now, f_trail);
        s.factor_to = std::max(f_now, f_trail);
    }
    s.active = s.factor != 1.0 || s.motion_blur;
    return s;
}

inline SlideState slide(const Params& now, const Params& prev, const Ctx& c) {
    SlideState s;
    const int n = op_slot(now, now.slide, c.fps);
    if (!n) return s;
    const uint32_t seed = op_seed(now, now.slide);
    const EnvelopePair env = envelope_pair(c.t, n, seed, c.ease_in, c.ease_out, c.mode);
    s.envelope = env.now;

    const double base = -1.0 - (now.slide_tendency - 1.0) * 0.5;
    const double dir = now.slide_direction_deg * kPi / 180.0;
    // Magnitude in % of the layer size.
    const double gain_now = c.gain * now.slide.amount * 2.0 * env.now;
    const double gain_prev = c.gain_prev * prev.slide.amount * 2.0 * env.prev;
    const double t0 = c.t - 1.0;

    auto angle = [&](double t, int k) {
        const uint32_t key = seed + stream::kSlideAngle + 1000u * k;
        return dir + (random_range(t, n, key, 0.0, c.mode) - 0.5) * now.slide_spread * 2.0 * kPi;
    };
    auto main_at = [&](double t, double gain, double rmin) {
        const double d = random_polar(t, n, seed + stream::kSlideSign, rmin, c.mode) + base;
        return polar_offset(gain * d, angle(t, 0), c.width, c.height);
    };
    auto channel_at = [&](double t, double gain, double rmin, int ch) {
        const uint32_t key = seed + stream::kSlideChannelAmp + 1000u * ch;
        const double d = random_range(t, n, key, rmin, c.mode) + base;
        return polar_offset(gain * d, angle(t, ch + 1), c.width, c.height);
    };

    const double split = now.slide_rgb_split * 0.01;
    const double mb = now.slide_motion_blur * 0.01;
    const Vec2 main_now = main_at(c.t, gain_now, c.rmin);
    const Vec2 main_prev = main_at(t0, gain_prev, c.rmin_prev);
    s.main = {lerp(main_now, main_prev, mb), main_now};
    for (int ch = 0; ch < 3; ++ch) {
        const Vec2 cn = lerp(main_now, channel_at(c.t, gain_now, c.rmin, ch), split);
        const Vec2 cp = lerp(main_prev, channel_at(t0, gain_prev, c.rmin_prev, ch), split);
        s.channel[ch] = {lerp(cn, cp, mb), cn};
    }

    auto moved = [](const Segment& g) {
        return g.to.x != 0.0 || g.to.y != 0.0 || g.from.x != g.to.x || g.from.y != g.to.y;
    };
    s.split_amount = split;
    s.rgb_split = split != 0.0 && (moved(s.channel[0]) || moved(s.channel[1]) || moved(s.channel[2]));
    s.motion_blur = mb > 0.0 && (s.main.from.x != s.main.to.x || s.main.from.y != s.main.to.y);
    s.active = s.rgb_split || moved(s.main);
    return s;
}

inline TimeState time(const Params& p, const Ctx& c) {
    TimeState s;
    const int n = op_slot(p, p.time, c.fps);
    if (!n) return s;
    const uint32_t seed = op_seed(p, p.time);
    s.envelope = envelope(c.t, n, seed, c.ease_in, c.ease_out, c.mode);
    double amt = c.gain * p.time.amount * s.envelope *
                 random_range(c.t, n, seed + stream::kTimeAmp, c.rmin, c.mode);
    const double u = random_range(c.t, n, seed + stream::kTimeDir, 0.0, c.mode);
    if (p.time_direction == TimeDirection::BackwardOnly ||
        (p.time_direction == TimeDirection::Both && u - 0.5 < 0.0)) {
        amt = -amt;
    }
    s.frame_offset = int(amt * c.fps * 0.01);
    s.active = s.frame_offset != 0;
    return s;
}

}  // namespace detail

// State of every operator at `frame`.
// `prev` holds the parameters evaluated one frame earlier (for Slide/Scale motion
// blur with keyframes); if nothing is animated, pass the same as `now`.
inline FrameState evaluate(const Params& now, const Params& prev, int64_t frame, double fps,
                           double width, double height, RandomMode mode = RandomMode::Stateless) {
    const detail::Ctx c{double(frame) + 0.5, fps,
                        width,               height,
                        now.amount * 0.01,   prev.amount * 0.01,
                        now.randomize_min * 0.01, prev.randomize_min * 0.01,
                        now.ease_in,         now.ease_out,
                        mode};
    FrameState f;
    f.blur = detail::blur(now, c);
    f.color = detail::color(now, c);
    f.light = detail::light(now, c);
    f.scale = detail::scale(now, prev, c);
    f.slide = detail::slide(now, prev, c);
    f.time = detail::time(now, c);
    return f;
}

inline FrameState evaluate(const Params& p, int64_t frame, double fps, double width,
                           double height, RandomMode mode = RandomMode::Stateless) {
    return evaluate(p, p, frame, fps, width, height, mode);
}

}  // namespace twr
