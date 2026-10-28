// bench_render — times the render of N frames at 1920×1080 with a common setup
// (Scale + Slide with motion blur).
//
// Usage: bench_render [frames] [scale_motion_blur%] [rgb_split%] [slide_motion_blur%]
#include <chrono>
#include <cstdio>
#include <cstdlib>

#include "twitch_render.h"

int main(int argc, char** argv) {
    const int frames = argc > 1 ? std::atoi(argv[1]) : 60;
    const int w = 1920, h = 1080;

    twr::Image src(w, h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            src.at(x, y) = {float((x * 7) % 255) / 255, float((y * 3) % 255) / 255, 0.5f, 1.0f};

    twr::Params p;
    p.speed = 5;
    p.amount = 35;
    p.blur.enabled = p.color.enabled = p.time.enabled = false;
    p.light.enabled = false;
    p.scale.enabled = p.slide.enabled = true;
    p.scale_motion_blur = argc > 2 ? std::atof(argv[2]) : 100;
    p.slide_rgb_split = argc > 3 ? std::atof(argv[3]) : 12;
    p.slide_motion_blur = argc > 4 ? std::atof(argv[4]) : 100;
    std::printf("threads: %u | scale mb %.0f%% | rgb split %.0f%% | slide mb %.0f%%\n",
                std::thread::hardware_concurrency(), p.scale_motion_blur, p.slide_rgb_split,
                p.slide_motion_blur);

    twr::Image dst(w, h);
    double busy_ms = 0, idle_ms = 0;
    int busy = 0, idle = 0;
    for (int f = 0; f < frames; ++f) {
        const twr::FrameState st = twr::evaluate(p, f, 30, w, h);
        const auto t0 = std::chrono::steady_clock::now();
        if (st.any_active()) twr::render_frame(src, dst, st, {}, twr::Border::Mirror);
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();
        (st.any_active() ? busy_ms : idle_ms) += ms;
        (st.any_active() ? busy : idle) += 1;
    }
    std::printf("frames with a twitch: %d, mean %.1f ms\n", busy, busy ? busy_ms / busy : 0.0);
    std::printf("frames without a twitch: %d (direct copy in the plugin)\n", idle);
    return 0;
}
