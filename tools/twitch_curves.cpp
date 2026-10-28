// twitch_curves — vuelca el estado por fotograma de cada operador a CSV.
//
// Uso: twitch_curves [opciones] > curvas.csv
//   --frames N        fotogramas a evaluar (defecto 120)
//   --fps F           (defecto 30)
//   --size W H        tamaño de la capa (defecto 1920 1080)
//   --legacy          patrón aleatorio compatible con Twitch
//   --speed S --amount A --ease-in E --ease-out E --randmin R --seed S
//   --tw OP V         Twitches [sec] de un operador (blur|color|light|scale|slide|time)
//   --amt OP V        Amount de un operador
//   --light-mode brighter|darker|both
//   --time-dir both|forward|backward
//   --spread V --tendency V --split V --slide-mb V --scale-mb V
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "twitch_core.h"

namespace {

twr::OperatorParams* find_op(twr::Params& p, const std::string& name) {
    if (name == "blur") return &p.blur;
    if (name == "color") return &p.color;
    if (name == "light") return &p.light;
    if (name == "scale") return &p.scale;
    if (name == "slide") return &p.slide;
    if (name == "time") return &p.time;
    return nullptr;
}

[[noreturn]] void fail(const char* msg, const char* arg) {
    std::fprintf(stderr, "twitch_curves: %s: %s\n", msg, arg);
    std::exit(2);
}

}  // namespace

int main(int argc, char** argv) {
    twr::Params p;
    int frames = 120;
    double fps = 30.0, width = 1920.0, height = 1080.0;
    twr::RandomMode mode = twr::RandomMode::Stateless;

    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        auto next = [&]() -> const char* {
            if (i + 1 >= argc) fail("falta valor para", argv[i]);
            return argv[++i];
        };
        auto num = [&]() { return std::atof(next()); };
        if (a == "--frames") frames = std::atoi(next());
        else if (a == "--fps") fps = num();
        else if (a == "--size") { width = num(); height = num(); }
        else if (a == "--legacy") mode = twr::RandomMode::Legacy;
        else if (a == "--speed") p.speed = num();
        else if (a == "--amount") p.amount = num();
        else if (a == "--ease-in") p.ease_in = num();
        else if (a == "--ease-out") p.ease_out = num();
        else if (a == "--randmin") p.randomize_min = num();
        else if (a == "--seed") p.seed = std::atoi(next());
        else if (a == "--tw" || a == "--amt") {
            twr::OperatorParams* op = find_op(p, next());
            if (!op) fail("operador desconocido", argv[i]);
            (a == "--tw" ? op->twitches_per_sec : op->amount) = num();
        }
        else if (a == "--light-mode") {
            const std::string v = next();
            p.light_mode = v == "darker" ? twr::LightMode::Darker
                         : v == "both"   ? twr::LightMode::Both
                                         : twr::LightMode::Brighter;
        }
        else if (a == "--time-dir") {
            const std::string v = next();
            p.time_direction = v == "forward"  ? twr::TimeDirection::ForwardOnly
                             : v == "backward" ? twr::TimeDirection::BackwardOnly
                                               : twr::TimeDirection::Both;
        }
        else if (a == "--spread") p.slide_spread = num();
        else if (a == "--tendency") p.slide_tendency = num();
        else if (a == "--split") p.slide_rgb_split = num();
        else if (a == "--slide-mb") p.slide_motion_blur = num();
        else if (a == "--scale-mb") p.scale_motion_blur = num();
        else fail("opción desconocida", argv[i]);
    }

    std::printf(
        "frame,blur_env,blur_x,blur_y,blur_mix,color_env,color_strength,light_env,light_gain,"
        "scale_env,scale_factor,scale_from,scale_to,slide_env,slide_x,slide_y,slide_from_x,"
        "slide_from_y,time_env,time_offset\n");
    for (int f = 0; f < frames; ++f) {
        const twr::FrameState s = twr::evaluate(p, f, fps, width, height, mode);
        std::printf("%d,%.6f,%.4f,%.4f,%.4f,%.6f,%.4f,%.6f,%.4f,%.6f,%.4f,%.4f,%.4f,%.6f,%.3f,%.3f,"
                    "%.3f,%.3f,%.6f,%d\n",
                    f, s.blur.envelope, s.blur.size_x, s.blur.size_y, s.blur.mix, s.color.envelope,
                    s.color.strength, s.light.envelope, s.light.gain, s.scale.envelope,
                    s.scale.factor, s.scale.factor_from, s.scale.factor_to, s.slide.envelope,
                    s.slide.main.to.x, s.slide.main.to.y, s.slide.main.from.x, s.slide.main.from.y,
                    s.time.envelope, s.time.frame_offset);
    }
    return 0;
}
