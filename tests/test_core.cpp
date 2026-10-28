// Core tests: they check the properties stated in docs/behaviour.md, not magic values.
#include <cmath>
#include <cstdio>

#include "flinch_core.h"

namespace {

int g_failures = 0;

#define CHECK(cond)                                                          \
    do {                                                                     \
        if (!(cond)) {                                                       \
            std::fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                    \
        }                                                                    \
    } while (0)

bool near(double a, double b, double eps = 1e-9) { return std::abs(a - b) <= eps; }

const flinch::RandomMode kModes[] = {flinch::RandomMode::Stateless, flinch::RandomMode::Legacy};

void test_legacy_rng_matches_msvc_lcg() {
    // srand(1); rand() → 41 in the MSVC CRT. Slot -5 is clamped to -4 → 4+5 discards.
    uint32_t state = 1;
    for (int k = 0; k < 10; ++k) state = state * 214013u + 2531011u;
    const double expected = double((state >> 16) & 0x7FFF) / 32767.0;
    CHECK(near(flinch::random01(-4, 1, flinch::RandomMode::Legacy), expected));
    CHECK(near(flinch::random01(-5, 1, flinch::RandomMode::Legacy), expected));
    // Correct constants: MSVC's first rand() after srand(1) is 41.
    static_assert((((1u * 214013u + 2531011u) >> 16) & 0x7FFF) == 41);
}

void test_random_in_range_and_deterministic() {
    for (auto mode : kModes) {
        for (int64_t slot = -3; slot < 200; ++slot) {
            const double u = flinch::random01(slot, 1234, mode);
            CHECK(u >= 0.0 && u <= 1.0);
            CHECK(u == flinch::random01(slot, 1234, mode));
        }
    }
}

void test_slot_length() {
    CHECK(flinch::slot_length(30, 1, 5) == 6);
    CHECK(flinch::slot_length(24, 1, 5) == 4);  // truncated: 4.8 → 4
    CHECK(flinch::slot_length(30, 50, 5) == 1);  // never below 1
    CHECK(flinch::slot_length(30, 0, 5) == 0);
    CHECK(flinch::slot_length(30, 1, 0) == 0);
}

void test_one_event_per_slot() {
    for (auto mode : kModes) {
        const int n = 6;
        for (int64_t i = 0; i < 100; ++i) {
            const double e = flinch::event_time(i, n, 77, mode);
            CHECK(e >= double(i * n) && e <= double((i + 1) * n));
        }
        for (double t = 0.5; t < 300; t += 0.37) {
            const flinch::Events ev = flinch::events_around(t, n, 77, mode);
            CHECK(ev.prev <= t && t < ev.next);
        }
    }
}

void test_envelope_shape() {
    for (auto mode : kModes) {
        const int n = 10;
        const double ease_in = 2, ease_out = 3;
        for (double t = 0.5; t < 500; t += 0.25) {
            const double env = flinch::envelope(t, n, 5, ease_in, ease_out, mode);
            CHECK(env >= 0.0 && env <= 1.0);
            const flinch::Events ev = flinch::events_around(t, n, 5, mode);
            const double expected = std::max(
                {0.0, 1.0 - (t - ev.prev) / (ease_out + 1), 1.0 - (ev.next - t) / (ease_in + 1)});
            CHECK(near(env, expected));
        }
        // Exactly at the event the envelope is 1.
        const double e = flinch::event_time(3, n, 5, mode);
        CHECK(near(flinch::envelope(e, n, 5, ease_in, ease_out, mode), 1.0));
    }
}

void test_random_range_respects_minimum() {
    for (auto mode : kModes) {
        for (double t = 0.5; t < 300; t += 1) {
            const double v = flinch::random_range(t, 6, 42, 0.7, mode);
            CHECK(v >= 0.7 - 1e-12 && v <= 1.0);
            const double w = flinch::random_polar(t, 6, 42, 1.0, mode);
            CHECK(w == 0.0 || w == 1.0);  // with minimum 1, Slide is always at full strength
        }
    }
}

void test_disabled_operators_are_idle() {
    flinch::Params p;
    p.speed = 0;
    for (int f = 0; f < 60; ++f) {
        const flinch::FrameState s = flinch::evaluate(p, f, 30, 1920, 1080);
        CHECK(!s.blur.active && !s.color.active && !s.light.active);
        CHECK(!s.scale.active && !s.slide.active && !s.time.active);
    }
}

void test_operator_ranges() {
    flinch::Params p;
    p.amount = 100;
    p.light.amount = 100;
    p.time.amount = 100;
    p.slide.amount = 50;
    p.scale.amount = 300;
    for (int f = 0; f < 600; ++f) {
        const flinch::FrameState s = flinch::evaluate(p, f, 30, 1000, 500);
        CHECK(s.light.gain >= 1.0 && s.light.gain <= 4.0 + 1e-9);  // Brighter: up to ×4
        CHECK(std::abs(s.time.frame_offset) <= 30);                // up to 1 s
        CHECK(s.scale.factor >= 0.25 && s.scale.factor <= 1.0);    // zoom ×1…×4
        CHECK(std::abs(s.slide.main.to.x) <= 500 + 1e-6);          // ≤ 50% of the width
        CHECK(std::abs(s.slide.main.to.y) <= 250 + 1e-6);
        CHECK(s.blur.mix >= 0.0 && s.blur.mix <= 1.0);
    }
}

void test_light_darker_is_inverse() {
    flinch::Params b, d;
    b.light_mode = flinch::LightMode::Brighter;
    d.light_mode = flinch::LightMode::Darker;
    for (int f = 0; f < 120; ++f) {
        const double gb = flinch::evaluate(b, f, 30, 100, 100).light.gain;
        const double gd = flinch::evaluate(d, f, 30, 100, 100).light.gain;
        CHECK(near(gb * gd, 1.0));
    }
}

void test_time_direction() {
    flinch::Params fwd, back;
    fwd.time_direction = flinch::TimeDirection::ForwardOnly;
    back.time_direction = flinch::TimeDirection::BackwardOnly;
    for (int f = 0; f < 300; ++f) {
        CHECK(flinch::evaluate(fwd, f, 30, 100, 100).time.frame_offset >= 0);
        CHECK(flinch::evaluate(back, f, 30, 100, 100).time.frame_offset <= 0);
    }
}

void test_unique_seeds_decorrelate_operators() {
    flinch::Params p;
    p.blur.unique_seed = 1;
    p.light.unique_seed = 2;
    int differ = 0;
    for (int f = 0; f < 300; ++f) {
        const flinch::FrameState s = flinch::evaluate(p, f, 30, 100, 100);
        differ += s.blur.envelope != s.light.envelope;
    }
    CHECK(differ > 100);
}

// AE convention: 0° moves vertically, 90° horizontally (verified against Twitch).
void test_slide_direction_follows_ae_compass() {
    flinch::Params p;
    p.slide_spread = 0;
    p.slide_tendency = -1;  // magnitude always >= 0 → toward the arrow
    p.slide_motion_blur = 0;
    for (double deg : {0.0, 90.0}) {
        p.slide_direction_deg = deg;
        int moved = 0;
        for (int f = 0; f < 120; ++f) {
            const flinch::Vec2 o = flinch::evaluate(p, f, 30, 1000, 1000).slide.main.to;
            if (deg == 0.0) {
                CHECK(std::abs(o.x) < 1e-9 && o.y <= 1e-9);  // up
            } else {
                CHECK(std::abs(o.y) < 1e-9 && o.x >= -1e-9);  // right
            }
            moved += std::abs(o.x) + std::abs(o.y) > 1.0;
        }
        CHECK(moved > 0);
    }
}

void test_slide_motion_blur_trail() {
    flinch::Params p;
    p.slide_motion_blur = 0;
    for (int f = 0; f < 120; ++f) {
        const flinch::SlideState s = flinch::evaluate(p, f, 30, 100, 100).slide;
        CHECK(s.main.from.x == s.main.to.x && s.main.from.y == s.main.to.y);
    }
}

}  // namespace

int main() {
    test_legacy_rng_matches_msvc_lcg();
    test_random_in_range_and_deterministic();
    test_slot_length();
    test_one_event_per_slot();
    test_envelope_shape();
    test_random_range_respects_minimum();
    test_disabled_operators_are_idle();
    test_operator_ranges();
    test_light_darker_is_inverse();
    test_time_direction();
    test_unique_seeds_decorrelate_operators();
    test_slide_direction_follows_ae_compass();
    test_slide_motion_blur_trail();
    if (g_failures) {
        std::fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("All tests passed\n");
    return 0;
}
