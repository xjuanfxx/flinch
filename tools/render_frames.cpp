// render_frames — renders Flinch over a still image, frame by frame, without
// After Effects. Used to make the animated banner (tools/make_banner.py).
//
// Usage: render_frames in.rgba out_prefix [options]
//   in.rgba / output frames: "FLRGBA <w> <h>\n" followed by 8-bit straight RGBA rows.
//   Frames are written as <out_prefix>NNNN.rgba.
//   --frames N --fps F --speed S --amount A --ease-in E --ease-out E --seed S
//   --tw OP V --amt OP V   Twitches [sec] / Amount of an operator (blur|light|scale|slide)
//   --split V --spread V --slide-mb V --scale-mb V
//   --idle                 only print the frames where something twitches, then exit
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

#include "flinch_render.h"

namespace {

[[noreturn]] void fail(const char* msg, const char* arg) {
    std::fprintf(stderr, "render_frames: %s: %s\n", msg, arg);
    std::exit(2);
}

flinch::OperatorParams* find_op(flinch::Params& p, const std::string& name) {
    if (name == "blur") return &p.blur;
    if (name == "light") return &p.light;
    if (name == "scale") return &p.scale;
    if (name == "slide") return &p.slide;
    return nullptr;
}

flinch::Image read_rgba(const char* path) {
    FILE* f = std::fopen(path, "rb");
    if (!f) fail("cannot open", path);
    int w = 0, h = 0;
    if (std::fscanf(f, "FLRGBA %d %d", &w, &h) != 2 || w <= 0 || h <= 0 || std::fgetc(f) != '\n')
        fail("bad header", path);
    std::vector<unsigned char> raw(size_t(w) * h * 4);
    if (std::fread(raw.data(), 1, raw.size(), f) != raw.size()) fail("truncated", path);
    std::fclose(f);
    flinch::Image img(w, h);
    for (size_t i = 0; i < img.px.size(); ++i) {
        const float a = raw[i * 4 + 3] / 255.0f;  // to premultiplied
        img.px[i] = {raw[i * 4] / 255.0f * a, raw[i * 4 + 1] / 255.0f * a, raw[i * 4 + 2] / 255.0f * a, a};
    }
    return img;
}

void write_rgba(const std::string& path, const flinch::Image& img) {
    FILE* f = std::fopen(path.c_str(), "wb");
    if (!f) fail("cannot write", path.c_str());
    std::fprintf(f, "FLRGBA %d %d\n", img.width, img.height);
    std::vector<unsigned char> raw(img.px.size() * 4);
    auto byte = [](float v) { return (unsigned char)(std::min(1.0f, std::max(0.0f, v)) * 255.0f + 0.5f); };
    for (size_t i = 0; i < img.px.size(); ++i) {
        const flinch::Pixel& p = img.px[i];
        const float inv = p.a > 0 ? 1.0f / p.a : 0.0f;  // back to straight alpha
        raw[i * 4] = byte(p.r * inv);
        raw[i * 4 + 1] = byte(p.g * inv);
        raw[i * 4 + 2] = byte(p.b * inv);
        raw[i * 4 + 3] = byte(p.a);
    }
    std::fwrite(raw.data(), 1, raw.size(), f);
    std::fclose(f);
}

}  // namespace

int main(int argc, char** argv) {
    if (argc < 3) fail("usage", "render_frames in.rgba out_prefix [options]");
    flinch::Params p;
    p.blur.enabled = p.color.enabled = p.light.enabled = false;
    p.scale.enabled = p.slide.enabled = p.time.enabled = false;
    int frames = 90;
    double fps = 30.0;
    bool idle_only = false;

    for (int i = 3; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> const char* {
            if (i + 1 >= argc) fail("missing value for", argv[i]);
            return argv[++i];
        };
        auto num = [&]() { return std::atof(next()); };
        if (a == "--frames") frames = std::atoi(next());
        else if (a == "--fps") fps = num();
        else if (a == "--speed") p.speed = num();
        else if (a == "--amount") p.amount = num();
        else if (a == "--ease-in") p.ease_in = num();
        else if (a == "--ease-out") p.ease_out = num();
        else if (a == "--seed") p.seed = std::atoi(next());
        else if (a == "--tw" || a == "--amt") {
            flinch::OperatorParams* op = find_op(p, next());
            if (!op) fail("unknown operator", argv[i]);
            op->enabled = true;
            (a == "--tw" ? op->twitches_per_sec : op->amount) = num();
        }
        else if (a == "--split") p.slide_rgb_split = num();
        else if (a == "--spread") p.slide_spread = num();
        else if (a == "--slide-mb") p.slide_motion_blur = num();
        else if (a == "--scale-mb") p.scale_motion_blur = num();
        else if (a == "--idle") idle_only = true;
        else fail("unknown option", argv[i]);
    }

    const flinch::Image src = read_rgba(argv[1]);
    p.scale_origin = {src.width * 0.5, src.height * 0.5};
    flinch::Image dst(src.width, src.height);
    for (int f = 0; f < frames; ++f) {
        const flinch::FrameState st = flinch::evaluate(p, f, fps, src.width, src.height);
        if (idle_only) {
            if (st.any_active()) std::printf("%d\n", f);
            continue;
        }
        char name[16];
        std::snprintf(name, sizeof name, "%04d.rgba", f);
        if (st.any_active()) {
            flinch::render_frame(src, dst, st, {}, flinch::Border::Transparent);
            write_rgba(argv[2] + std::string(name), dst);
        } else {
            write_rgba(argv[2] + std::string(name), src);
        }
    }
    return 0;
}
