// Render tests: geometry, borders, blur and tone on synthetic images.
#include <cmath>
#include <cstdio>

#include "flinch_render.h"

namespace {

int g_failures = 0;

#define CHECK(cond)                                                                \
    do {                                                                           \
        if (!(cond)) {                                                             \
            std::fprintf(stderr, "%s:%d: CHECK(%s)\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                          \
        }                                                                          \
    } while (0)

bool near(float a, float b, float eps = 1e-4f) { return std::abs(a - b) <= eps; }

// Opaque image with a horizontal gradient in R and a vertical one in G.
flinch::Image gradient(int w, int h) {
    flinch::Image img(w, h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) img.at(x, y) = {float(x) / w, float(y) / h, 0.25f, 1.0f};
    return img;
}

void test_identity_when_idle() {
    const flinch::Image src = gradient(32, 16);
    flinch::Image dst(32, 16);
    flinch::render_frame(src, dst, flinch::FrameState{}, {}, flinch::Border::Mirror);
    for (size_t i = 0; i < src.px.size(); ++i) {
        CHECK(near(dst.px[i].r, src.px[i].r) && near(dst.px[i].g, src.px[i].g));
        CHECK(near(dst.px[i].a, 1.0f));
    }
}

void test_slide_shifts_image() {
    const flinch::Image src = gradient(40, 10);
    flinch::Image dst(40, 10);
    flinch::FrameState st;
    st.slide.active = true;
    st.slide.main = {{5, 0}, {5, 0}};
    flinch::render_frame(src, dst, st, {}, flinch::Border::Transparent);
    CHECK(near(dst.at(20, 3).r, src.at(15, 3).r));  // content moved 5 px to the right
    CHECK(near(dst.at(2, 3).a, 0.0f));              // empty space comes in from the left
}

void test_borders() {
    const flinch::Image src = gradient(10, 4);
    flinch::FrameState st;
    st.slide.active = true;
    st.slide.main = {{3, 0}, {3, 0}};
    flinch::Image mirror(10, 4), tile(10, 4), clamp(10, 4);
    flinch::render_frame(src, mirror, st, {}, flinch::Border::Mirror);
    flinch::render_frame(src, tile, st, {}, flinch::Border::Tile);
    flinch::render_frame(src, clamp, st, {}, flinch::Border::Clamp);
    CHECK(near(mirror.at(0, 1).r, src.at(2, 1).r));  // x=-3 → mirror → 2
    CHECK(near(tile.at(0, 1).r, src.at(7, 1).r));    // x=-3 → tile → 7
    CHECK(near(clamp.at(0, 1).r, src.at(0, 1).r));   // x=-3 → edge → 0
}

void test_scale_zooms_about_origin() {
    const flinch::Image src = gradient(64, 64);
    flinch::Image dst(64, 64);
    flinch::FrameState st;
    st.scale.active = true;
    st.scale.factor = st.scale.factor_from = st.scale.factor_to = 0.5;  // zoom ×2
    st.scale.origin = {32, 32};
    flinch::render_frame(src, dst, st, {}, flinch::Border::Mirror);
    CHECK(near(dst.at(32, 32).r, src.at(32, 32).r, 0.02f));  // the origin does not move
    CHECK(near(dst.at(48, 32).r, src.at(40, 32).r, 0.02f));  // 16 px from the origin → 8 in the source
}

void test_rgb_split_separates_channels() {
    flinch::Image src(20, 4);
    src.at(10, 1) = src.at(10, 2) = {1, 1, 1, 1};
    flinch::Image dst(20, 4);
    flinch::FrameState st;
    st.slide.active = st.slide.rgb_split = true;
    st.slide.channel[0] = {{-3, 0}, {-3, 0}};
    st.slide.channel[1] = {{0, 0}, {0, 0}};
    st.slide.channel[2] = {{3, 0}, {3, 0}};
    flinch::render_frame(src, dst, st, {}, flinch::Border::Transparent);
    CHECK(near(dst.at(7, 1).r, 1) && near(dst.at(7, 1).g, 0));
    CHECK(near(dst.at(10, 1).g, 1) && near(dst.at(10, 1).r, 0));
    CHECK(near(dst.at(13, 1).b, 1) && near(dst.at(13, 1).g, 0));
}

void test_motion_blur_spreads_energy() {
    flinch::Image src(40, 3);
    src.at(10, 1) = {1, 1, 1, 1};
    flinch::Image dst(40, 3);
    flinch::FrameState st;
    st.slide.active = st.slide.motion_blur = true;
    st.slide.main = {{0, 0}, {10, 0}};  // from t-1 (0 px) to t (10 px)
    flinch::render_frame(src, dst, st, {}, flinch::Border::Transparent);
    float total = 0;
    int lit = 0;
    for (int x = 0; x < 40; ++x) {
        total += dst.at(x, 1).r;
        lit += dst.at(x, 1).r > 0.01f;
    }
    CHECK(near(total, 1.0f, 0.05f));  // energy is preserved
    CHECK(lit >= 8);                  // and spread along the trail
}

void test_blur_and_tone() {
    flinch::Image img(21, 5);
    img.at(10, 2) = {1, 1, 1, 1};
    flinch::BlurState b;
    b.active = true;
    b.size_x = 10;  // radius 5 in X
    b.opacity = 1;
    b.boost = 1;
    flinch::FrameState st;
    st.blur = b;
    flinch::Image dst(21, 5);
    flinch::render_frame(img, dst, st, {}, flinch::Border::Transparent);
    CHECK(near(dst.at(10, 2).r, 1.0f / 11));
    CHECK(near(dst.at(15, 2).r, 1.0f / 11));
    CHECK(near(dst.at(16, 2).r, 0.0f));
    CHECK(near(dst.at(10, 1).r, 0.0f));  // no vertical blur

    flinch::Image flat(4, 4);
    for (auto& p : flat.px) p = {0.2f, 0.2f, 0.2f, 1};
    flinch::FrameState light;
    light.light.active = true;
    light.light.gain = 2.0;
    flinch::Image out(4, 4);
    flinch::render_frame(flat, out, light, {}, flinch::Border::Mirror);
    CHECK(near(out.at(1, 1).r, 0.4f) && near(out.at(1, 1).a, 1.0f));
}

}  // namespace

int main() {
    test_identity_when_idle();
    test_slide_shifts_image();
    test_borders();
    test_scale_zooms_about_origin();
    test_rgb_split_separates_channels();
    test_motion_blur_spreads_energy();
    test_blur_and_tone();
    if (g_failures) {
        std::fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("All render tests passed\n");
    return 0;
}
