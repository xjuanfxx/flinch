// Tests del núcleo: comprueban las propiedades de la spec, no valores mágicos.
#include <cmath>
#include <cstdio>

#include "twitch_core.h"

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

const twr::RandomMode kModes[] = {twr::RandomMode::Stateless, twr::RandomMode::Legacy};

void test_legacy_rng_matches_msvc_lcg() {
    // srand(1); rand() → 41 en la CRT de MSVC. Slot -5 se fija a -4 → 4+5 descartes.
    uint32_t state = 1;
    for (int k = 0; k < 10; ++k) state = state * 214013u + 2531011u;
    const double expected = double((state >> 16) & 0x7FFF) / 32767.0;
    CHECK(near(twr::random01(-4, 1, twr::RandomMode::Legacy), expected));
    CHECK(near(twr::random01(-5, 1, twr::RandomMode::Legacy), expected));
    // Constantes correctas: el primer rand() de MSVC tras srand(1) es 41.
    static_assert((((1u * 214013u + 2531011u) >> 16) & 0x7FFF) == 41);
}

void test_random_in_range_and_deterministic() {
    for (auto mode : kModes) {
        for (int64_t slot = -3; slot < 200; ++slot) {
            const double u = twr::random01(slot, 1234, mode);
            CHECK(u >= 0.0 && u <= 1.0);
            CHECK(u == twr::random01(slot, 1234, mode));
        }
    }
}

void test_slot_length() {
    CHECK(twr::slot_length(30, 1, 5) == 6);
    CHECK(twr::slot_length(24, 1, 5) == 4);  // truncado: 4.8 → 4
    CHECK(twr::slot_length(30, 50, 5) == 1);  // nunca menos de 1
    CHECK(twr::slot_length(30, 0, 5) == 0);
    CHECK(twr::slot_length(30, 1, 0) == 0);
}

void test_one_event_per_slot() {
    for (auto mode : kModes) {
        const int n = 6;
        for (int64_t i = 0; i < 100; ++i) {
            const double e = twr::event_time(i, n, 77, mode);
            CHECK(e >= double(i * n) && e <= double((i + 1) * n));
        }
        for (double t = 0.5; t < 300; t += 0.37) {
            const twr::Events ev = twr::events_around(t, n, 77, mode);
            CHECK(ev.prev <= t && t < ev.next);
        }
    }
}

void test_envelope_shape() {
    for (auto mode : kModes) {
        const int n = 10;
        const double ease_in = 2, ease_out = 3;
        for (double t = 0.5; t < 500; t += 0.25) {
            const double env = twr::envelope(t, n, 5, ease_in, ease_out, mode);
            CHECK(env >= 0.0 && env <= 1.0);
            const twr::Events ev = twr::events_around(t, n, 5, mode);
            const double expected = std::max(
                {0.0, 1.0 - (t - ev.prev) / (ease_out + 1), 1.0 - (ev.next - t) / (ease_in + 1)});
            CHECK(near(env, expected));
        }
        // Justo en el evento la envolvente vale 1.
        const double e = twr::event_time(3, n, 5, mode);
        CHECK(near(twr::envelope(e, n, 5, ease_in, ease_out, mode), 1.0));
    }
}

void test_random_range_respects_minimum() {
    for (auto mode : kModes) {
        for (double t = 0.5; t < 300; t += 1) {
            const double v = twr::random_range(t, 6, 42, 0.7, mode);
            CHECK(v >= 0.7 - 1e-12 && v <= 1.0);
            const double w = twr::random_polar(t, 6, 42, 1.0, mode);
            CHECK(w == 0.0 || w == 1.0);  // con mínimo 1, Slide siempre a tope
        }
    }
}

void test_disabled_operators_are_idle() {
    twr::Params p;
    p.speed = 0;
    for (int f = 0; f < 60; ++f) {
        const twr::FrameState s = twr::evaluate(p, f, 30, 1920, 1080);
        CHECK(!s.blur.active && !s.color.active && !s.light.active);
        CHECK(!s.scale.active && !s.slide.active && !s.time.active);
    }
}

void test_operator_ranges() {
    twr::Params p;
    p.amount = 100;
    p.light.amount = 100;
    p.time.amount = 100;
    p.slide.amount = 50;
    p.scale.amount = 300;
    for (int f = 0; f < 600; ++f) {
        const twr::FrameState s = twr::evaluate(p, f, 30, 1000, 500);
        CHECK(s.light.gain >= 1.0 && s.light.gain <= 4.0 + 1e-9);  // Brighter: hasta ×4
        CHECK(std::abs(s.time.frame_offset) <= 30);                // hasta 1 s
        CHECK(s.scale.factor >= 0.25 && s.scale.factor <= 1.0);    // zoom ×1…×4
        CHECK(std::abs(s.slide.main.to.x) <= 500 + 1e-6);          // ≤ 50% del ancho
        CHECK(std::abs(s.slide.main.to.y) <= 250 + 1e-6);
        CHECK(s.blur.mix >= 0.0 && s.blur.mix <= 1.0);
    }
}

void test_light_darker_is_inverse() {
    twr::Params b, d;
    b.light_mode = twr::LightMode::Brighter;
    d.light_mode = twr::LightMode::Darker;
    for (int f = 0; f < 120; ++f) {
        const double gb = twr::evaluate(b, f, 30, 100, 100).light.gain;
        const double gd = twr::evaluate(d, f, 30, 100, 100).light.gain;
        CHECK(near(gb * gd, 1.0));
    }
}

void test_time_direction() {
    twr::Params fwd, back;
    fwd.time_direction = twr::TimeDirection::ForwardOnly;
    back.time_direction = twr::TimeDirection::BackwardOnly;
    for (int f = 0; f < 300; ++f) {
        CHECK(twr::evaluate(fwd, f, 30, 100, 100).time.frame_offset >= 0);
        CHECK(twr::evaluate(back, f, 30, 100, 100).time.frame_offset <= 0);
    }
}

void test_unique_seeds_decorrelate_operators() {
    twr::Params p;
    p.blur.unique_seed = 1;
    p.light.unique_seed = 2;
    int differ = 0;
    for (int f = 0; f < 300; ++f) {
        const twr::FrameState s = twr::evaluate(p, f, 30, 100, 100);
        differ += s.blur.envelope != s.light.envelope;
    }
    CHECK(differ > 100);
}

// Convención de AE: 0° desplaza en vertical, 90° en horizontal (verificado contra Twitch).
void test_slide_direction_follows_ae_compass() {
    twr::Params p;
    p.slide_spread = 0;
    p.slide_tendency = -1;  // magnitud siempre >= 0 → hacia donde apunta la flecha
    p.slide_motion_blur = 0;
    for (double deg : {0.0, 90.0}) {
        p.slide_direction_deg = deg;
        int moved = 0;
        for (int f = 0; f < 120; ++f) {
            const twr::Vec2 o = twr::evaluate(p, f, 30, 1000, 1000).slide.main.to;
            if (deg == 0.0) {
                CHECK(std::abs(o.x) < 1e-9 && o.y <= 1e-9);  // hacia arriba
            } else {
                CHECK(std::abs(o.y) < 1e-9 && o.x >= -1e-9);  // hacia la derecha
            }
            moved += std::abs(o.x) + std::abs(o.y) > 1.0;
        }
        CHECK(moved > 0);
    }
}

void test_slide_motion_blur_trail() {
    twr::Params p;
    p.slide_motion_blur = 0;
    for (int f = 0; f < 120; ++f) {
        const twr::SlideState s = twr::evaluate(p, f, 30, 100, 100).slide;
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
        std::fprintf(stderr, "%d fallo(s)\n", g_failures);
        return 1;
    }
    std::printf("Todos los tests pasan\n");
    return 0;
}
