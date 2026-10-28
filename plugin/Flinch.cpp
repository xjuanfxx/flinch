// Flinch.cpp — After Effects effect (SmartFX, 8/16/32 bpc).
//
// Thin adapter: read parameters → flinch::evaluate (core) → flinch::render_frame.
// All operator logic lives in core/include.
#include "Flinch.h"

#include <cstring>
#include <cstdio>
#include <memory>
#include <string>

#include "AE_EffectGPUSuites.h"
#include "FlinchGpuRender.h"
#include "flinch_core.h"
#include "flinch_render.h"

namespace {

constexpr A_long kInputCheckout = 0;

// What PreRender computes and SmartRender consumes.
struct RenderInfo {
    flinch::FrameState state;
    flinch::Border border = flinch::Border::Mirror;
    double pixel_scale = 1.0;
    PF_LRect src_rect{};  // input buffer, in layer coords (downsampled)
    PF_LRect canvas{};    // working canvas: full layer ∪ input
    PF_LRect out_rect{};  // output rect returned to AE, in layer coords
    bool show_twitch_only = false;  // transparent on frames without a twitch
};

void delete_render_info(void* p) { delete static_cast<RenderInfo*>(p); }

bool empty_rect(const PF_LRect& r) { return r.right <= r.left || r.bottom <= r.top; }

PF_LRect union_rect(const PF_LRect& a, const PF_LRect& b) {
    if (empty_rect(a)) return b;
    if (empty_rect(b)) return a;
    return {std::min(a.left, b.left), std::min(a.top, b.top), std::max(a.right, b.right),
            std::max(a.bottom, b.bottom)};
}

PF_LRect intersect_rect(const PF_LRect& a, const PF_LRect& b) {
    PF_LRect r{std::max(a.left, b.left), std::max(a.top, b.top), std::min(a.right, b.right),
               std::min(a.bottom, b.bottom)};
    if (r.right < r.left) r.right = r.left;
    if (r.bottom < r.top) r.bottom = r.top;
    return r;
}

// ---------------------------------------------------------------------------
// Parameters
// ---------------------------------------------------------------------------

// Number with two decimals (e.g. "30.00"), as Twitch shows it.
PF_Err add_float(PF_InData* in_data, const char* name, double valid_min, double valid_max,
                 double slider_min, double slider_max, double dflt, int index) {
    PF_ParamDef def;
    AEFX_CLR_STRUCT(def);
    PF_ADD_FLOAT_SLIDERX(name, valid_min, valid_max, slider_min, slider_max, dflt,
                         PF_Precision_HUNDREDTHS, PF_ValueDisplayFlag_NONE, 0, disk_id(index));
    return PF_Err_NONE;
}

PF_Err add_int(PF_InData* in_data, const char* name, int lo, int hi, int dflt, int index) {
    PF_ParamDef def;
    AEFX_CLR_STRUCT(def);
    PF_ADD_SLIDER(name, lo, hi, lo, hi, dflt, disk_id(index));
    return PF_Err_NONE;
}

// Supervised: AE notifies us on change so the operator group can be dimmed.
PF_Err add_checkbox(PF_InData* in_data, const char* name, bool dflt, int index) {
    PF_ParamDef def;
    PF_ADD_CHECKBOXX(name, dflt ? TRUE : FALSE, PF_ParamFlag_SUPERVISE, disk_id(index));
    return PF_Err_NONE;
}

PF_Err add_popup(PF_InData* in_data, const char* name, int choices, int dflt, const char* items,
                 int index) {
    PF_ParamDef def;
    AEFX_CLR_STRUCT(def);
    PF_ADD_POPUP(name, choices, A_short(dflt), items, disk_id(index));
    return PF_Err_NONE;
}

PF_Err add_color(PF_InData* in_data, const char* name, A_u_char r, A_u_char g, A_u_char b,
                 int index) {
    PF_ParamDef def;
    AEFX_CLR_STRUCT(def);
    PF_ADD_COLOR(name, r, g, b, disk_id(index));
    return PF_Err_NONE;
}

// `collapsed`: the group starts folded when the effect is applied (as in Twitch).
PF_Err begin_group(PF_InData* in_data, const char* name, int index, bool collapsed = false) {
    PF_ParamDef def;
    AEFX_CLR_STRUCT(def);
    if (collapsed) def.flags = PF_ParamFlag_START_COLLAPSED;
    PF_ADD_TOPIC(name, disk_id(index));
    return PF_Err_NONE;
}

PF_Err end_group(PF_InData* in_data, int index) {
    PF_ParamDef def;
    AEFX_CLR_STRUCT(def);
    PF_END_TOPIC(disk_id(index));
    return PF_Err_NONE;
}

// Common block of every operator: "<Op> Amount" and "<Op> Twitches [sec]".
PF_Err add_operator_head(PF_InData* in_data, const char* name, int start) {
    PF_Err err = PF_Err_NONE;
    const std::string op = name;
    ERR(begin_group(in_data, name, start, true));
    ERR(add_float(in_data, (op + " Amount").c_str(), 0, 10000, 0, 100, 50, start + 1));
    ERR(add_float(in_data, (op + " Twitches [sec]").c_str(), 0, 1000, 0, 50, 1, start + 2));
    return err;
}

// Names, order and ranges follow Twitch's UI; values are plain numbers, not percentages.
PF_Err ParamsSetup(PF_InData* in_data, PF_OutData* out_data) {
    PF_Err err = PF_Err_NONE;

    ERR(add_float(in_data, "Amount", 0, 10000, 0, 500, 100, P_AMOUNT));
    ERR(add_float(in_data, "Speed", 0, 10000, 0, 500, 5, P_SPEED));

    // As in Twitch, every operator starts disabled.
    ERR(begin_group(in_data, "Enable", P_ENABLE_START, true));
    ERR(add_checkbox(in_data, "Blur", false, P_BLUR_ENABLE));
    ERR(add_checkbox(in_data, "Color", false, P_COLOR_ENABLE));
    ERR(add_checkbox(in_data, "Light", false, P_LIGHT_ENABLE));
    ERR(add_checkbox(in_data, "Scale", false, P_SCALE_ENABLE));
    ERR(add_checkbox(in_data, "Slide", false, P_SLIDE_ENABLE));
    ERR(add_checkbox(in_data, "Time", false, P_TIME_ENABLE));
    ERR(end_group(in_data, P_ENABLE_END));

    ERR(begin_group(in_data, "Behaviour", P_BEHAVIOUR_START));
    ERR(add_float(in_data, "Ease In", 0, 100, 0, 5, 1, P_EASE_IN));
    ERR(add_float(in_data, "Ease Out", 0, 100, 0, 5, 1, P_EASE_OUT));
    ERR(add_popup(in_data, "Border", 4, 1, "Mirror|Tile|Transparent|Stretch Edges", P_BORDER));
    ERR(add_checkbox(in_data, "Show Twitch Only", false, P_SHOW_TWITCH_ONLY));
    ERR(add_int(in_data, "Seed", 0, 999, 0, P_SEED));
    ERR(add_float(in_data, "Randomize Minimum", 0, 100, 0, 100, 0, P_RAND_MIN));
    ERR(add_popup(in_data, "Random Pattern", 2, 1, "Modern|Twitch Compatible", P_PATTERN));
    // GPU when After Effects renders on the GPU and the kernels load; otherwise the CPU.
    ERR(add_popup(in_data, "Render Using", 2, RENDER_GPU_OR_CPU, "GPU (CPU fallback)|CPU Only",
                  P_RENDER_USING));
    ERR(end_group(in_data, P_BEHAVIOUR_END));

    ERR(begin_group(in_data, "Operator Controls", P_OPERATORS_START, true));

    ERR(add_operator_head(in_data, "Blur", P_BLUR_START));
    ERR(add_color(in_data, "Blur Tint", 255, 255, 255, P_BLUR_TINT));
    ERR(add_float(in_data, "Blur Boost", 0, 1000, 0, 500, 100, P_BLUR_BOOST));
    ERR(add_float(in_data, "Blur Opacity", 0, 100, 0, 100, 100, P_BLUR_OPACITY));
    ERR(add_float(in_data, "Blur Aspect", -1, 1, -1, 1, 0, P_BLUR_ASPECT));
    ERR(add_int(in_data, "Unique Blur Seed", 0, 999, 0, P_BLUR_SEED));
    ERR(end_group(in_data, P_BLUR_END));

    ERR(add_operator_head(in_data, "Color", P_COLOR_START));
    ERR(add_color(in_data, "Colorize", 255, 162, 0, P_COLOR_COLORIZE));
    ERR(add_float(in_data, "Color Randomize", 0, 100, 0, 100, 0, P_COLOR_RANDOMIZE));
    ERR(add_int(in_data, "Unique Color Seed", 0, 999, 0, P_COLOR_SEED));
    ERR(end_group(in_data, P_COLOR_END));

    ERR(add_operator_head(in_data, "Light", P_LIGHT_START));
    ERR(add_popup(in_data, "Light Behaviour", 3, 1, "Brighter|Darker|Both", P_LIGHT_MODE));
    ERR(add_int(in_data, "Unique Light Seed", 0, 999, 0, P_LIGHT_SEED));
    ERR(end_group(in_data, P_LIGHT_END));

    ERR(add_operator_head(in_data, "Scale", P_SCALE_START));
    {
        PF_ParamDef def;
        AEFX_CLR_STRUCT(def);
        PF_ADD_POINT("Scale Origin", 50, 50, FALSE, disk_id(P_SCALE_ORIGIN));
    }
    ERR(add_float(in_data, "Scale Origin Randomize", 0, 100, 0, 100, 0, P_SCALE_ORIGIN_RAND));
    ERR(add_float(in_data, "Scale Motion Blur", 0, 100, 0, 100, 0, P_SCALE_MOTION_BLUR));
    ERR(add_int(in_data, "Unique Scale Seed", 0, 999, 0, P_SCALE_SEED));
    ERR(end_group(in_data, P_SCALE_END));

    ERR(add_operator_head(in_data, "Slide", P_SLIDE_START));
    {
        PF_ParamDef def;
        AEFX_CLR_STRUCT(def);
        PF_ADD_ANGLE("Slide Direction", 0, disk_id(P_SLIDE_DIRECTION));
    }
    ERR(add_float(in_data, "Slide Spread", 0, 1, 0, 1, 1, P_SLIDE_SPREAD));
    ERR(add_float(in_data, "Slide Tendency", -1, 1, -1, 1, 0, P_SLIDE_TENDENCY));
    ERR(add_float(in_data, "Slide RGB Split", 0, 100, 0, 100, 0, P_SLIDE_RGB_SPLIT));
    ERR(add_float(in_data, "Slide Motion Blur", 0, 100, 0, 100, 100, P_SLIDE_MOTION_BLUR));
    ERR(add_int(in_data, "Unique Slide Seed", 0, 999, 0, P_SLIDE_SEED));
    ERR(end_group(in_data, P_SLIDE_END));

    ERR(add_operator_head(in_data, "Time", P_TIME_START));
    ERR(add_popup(in_data, "Time Direction", 3, 1, "Both|Forward Only|Backward Only", P_TIME_DIRECTION));
    ERR(add_int(in_data, "Unique Time Seed", 0, 999, 0, P_TIME_SEED));
    ERR(end_group(in_data, P_TIME_END));

    ERR(end_group(in_data, P_OPERATORS_END));

    out_data->num_params = P_NUM_PARAMS;
    return err;
}

// Dims the group of every operator disabled in "Enable", like Twitch does.
PF_Err UpdateParamsUI(PF_InData* in_data, PF_ParamDef* params[]) {
    struct Link {
        int enable, group;
    };
    static constexpr Link kLinks[] = {
        {P_BLUR_ENABLE, P_BLUR_START},   {P_COLOR_ENABLE, P_COLOR_START},
        {P_LIGHT_ENABLE, P_LIGHT_START}, {P_SCALE_ENABLE, P_SCALE_START},
        {P_SLIDE_ENABLE, P_SLIDE_START}, {P_TIME_ENABLE, P_TIME_START},
    };
    PF_Err err = PF_Err_NONE;
    AEGP_SuiteHandler suites(in_data->pica_basicP);
    for (const Link& link : kLinks) {
        const bool enabled = params[link.enable]->u.bd.value != 0;
        PF_ParamDef def = *params[link.group];
        const bool disabled_now = (def.ui_flags & PF_PUI_DISABLED) != 0;
        if (disabled_now == !enabled) continue;
        if (enabled) def.ui_flags &= ~PF_PUI_DISABLED;
        else def.ui_flags |= PF_PUI_DISABLED;
        ERR(suites.ParamUtilsSuite3()->PF_UpdateParamUI(in_data->effect_ref, link.group, &def));
    }
    return err;
}

// ---------------------------------------------------------------------------
// Reading parameters at a given time
// ---------------------------------------------------------------------------

class ParamReader {
public:
    ParamReader(PF_InData* in_data, A_long time) : in_data_(in_data), time_(time) {}

    PF_Err checkout(int index, PF_ParamDef& def) {
        AEFX_CLR_STRUCT(def);
        return PF_CHECKOUT_PARAM(in_data_, index, time_, in_data_->time_step, in_data_->time_scale, &def);
    }
    double fval(int i, PF_Err& err) { PF_ParamDef d; err |= checkout(i, d); return d.u.fs_d.value; }
    int ival(int i, PF_Err& err) { PF_ParamDef d; err |= checkout(i, d); return d.u.sd.value; }
    bool bval(int i, PF_Err& err) { PF_ParamDef d; err |= checkout(i, d); return d.u.bd.value != 0; }
    int popup(int i, PF_Err& err) { PF_ParamDef d; err |= checkout(i, d); return d.u.pd.value; }
    double angle(int i, PF_Err& err) { PF_ParamDef d; err |= checkout(i, d); return FIX_2_FLOAT(d.u.ad.value); }
    flinch::Rgb color(int i, PF_Err& err) {
        PF_ParamDef d;
        err |= checkout(i, d);
        return {d.u.cd.value.red / 255.0, d.u.cd.value.green / 255.0, d.u.cd.value.blue / 255.0};
    }
    flinch::Vec2 point(int i, PF_Err& err) {
        PF_ParamDef d;
        err |= checkout(i, d);
        return {FIX_2_FLOAT(d.u.td.x_value), FIX_2_FLOAT(d.u.td.y_value)};
    }

private:
    PF_InData* in_data_;
    A_long time_;
};

void read_operator(ParamReader& r, int enable, int start, flinch::OperatorParams& op, int seed_index,
                   PF_Err& err) {
    op.enabled = r.bval(enable, err);
    op.amount = r.fval(start + 1, err);
    op.twitches_per_sec = r.fval(start + 2, err);
    op.unique_seed = r.ival(seed_index, err);
}

// `pixel_scale` converts the Origin point (downsampled coords) to full resolution.
flinch::Params read_params(PF_InData* in_data, A_long time, double pixel_scale, PF_Err& err) {
    ParamReader r(in_data, time);
    flinch::Params p;
    p.amount = r.fval(P_AMOUNT, err);
    p.speed = r.fval(P_SPEED, err);
    p.ease_in = r.fval(P_EASE_IN, err);
    p.ease_out = r.fval(P_EASE_OUT, err);
    p.randomize_min = r.fval(P_RAND_MIN, err);
    p.seed = r.ival(P_SEED, err);

    read_operator(r, P_BLUR_ENABLE, P_BLUR_START, p.blur, P_BLUR_SEED, err);
    p.blur_aspect = r.fval(P_BLUR_ASPECT, err);
    p.blur_boost = r.fval(P_BLUR_BOOST, err);
    p.blur_opacity = r.fval(P_BLUR_OPACITY, err);
    p.blur_tint = r.color(P_BLUR_TINT, err);

    read_operator(r, P_COLOR_ENABLE, P_COLOR_START, p.color, P_COLOR_SEED, err);
    p.colorize = r.color(P_COLOR_COLORIZE, err);
    p.color_randomize = r.fval(P_COLOR_RANDOMIZE, err);

    read_operator(r, P_LIGHT_ENABLE, P_LIGHT_START, p.light, P_LIGHT_SEED, err);
    p.light_mode = static_cast<flinch::LightMode>(r.popup(P_LIGHT_MODE, err));

    read_operator(r, P_SCALE_ENABLE, P_SCALE_START, p.scale, P_SCALE_SEED, err);
    const flinch::Vec2 origin = r.point(P_SCALE_ORIGIN, err);
    p.scale_origin = {origin.x / pixel_scale, origin.y / pixel_scale};
    p.scale_origin_randomize = r.fval(P_SCALE_ORIGIN_RAND, err);
    p.scale_motion_blur = r.fval(P_SCALE_MOTION_BLUR, err);

    read_operator(r, P_SLIDE_ENABLE, P_SLIDE_START, p.slide, P_SLIDE_SEED, err);
    p.slide_direction_deg = r.angle(P_SLIDE_DIRECTION, err);
    p.slide_spread = r.fval(P_SLIDE_SPREAD, err);
    p.slide_tendency = r.fval(P_SLIDE_TENDENCY, err);
    p.slide_rgb_split = r.fval(P_SLIDE_RGB_SPLIT, err);
    p.slide_motion_blur = r.fval(P_SLIDE_MOTION_BLUR, err);

    read_operator(r, P_TIME_ENABLE, P_TIME_START, p.time, P_TIME_SEED, err);
    p.time_direction = static_cast<flinch::TimeDirection>(r.popup(P_TIME_DIRECTION, err));
    return p;
}

// ---------------------------------------------------------------------------
// Commands
// ---------------------------------------------------------------------------

PF_Err About(PF_InData* in_data, PF_OutData* out_data) {
    AEGP_SuiteHandler suites(in_data->pica_basicP);
    suites.ANSICallbacksSuite1()->sprintf(out_data->return_msg, "%s v%d.%d\rRandom twitches: blur, color, light, scale, slide and time.",
                                          FLINCH_NAME, FLINCH_MAJOR_VERSION, FLINCH_MINOR_VERSION);
    return PF_Err_NONE;
}

PF_Err GlobalSetup(PF_OutData* out_data) {
    out_data->my_version =
        PF_VERSION(FLINCH_MAJOR_VERSION, FLINCH_MINOR_VERSION, FLINCH_BUG_VERSION, PF_Stage_DEVELOP, 1);
    out_data->out_flags = FLINCH_OUT_FLAGS;
    out_data->out_flags2 = FLINCH_OUT_FLAGS2;
    return PF_Err_NONE;
}

PF_Err PreRender(PF_InData* in_data, PF_OutData* /*out_data*/, PF_PreRenderExtra* extra) {
    PF_Err err = PF_Err_NONE;
    auto info = std::make_unique<RenderInfo>();

    const double ds_x = double(in_data->downsample_x.num) / in_data->downsample_x.den;
    info->pixel_scale = ds_x;

    const A_long now = in_data->current_time;
    const A_long step = in_data->time_step;
    flinch::Params p_now = read_params(in_data, now, ds_x, err);
    flinch::Params p_prev = read_params(in_data, now - step, ds_x, err);
    if (err) return err;

    ParamReader r(in_data, now);
    info->border = static_cast<flinch::Border>(r.popup(P_BORDER, err));
    info->show_twitch_only = r.bval(P_SHOW_TWITCH_ONLY, err);
    const flinch::RandomMode mode =
        r.popup(P_PATTERN, err) == 2 ? flinch::RandomMode::Legacy : flinch::RandomMode::Stateless;

    // Offer the GPU render only when AE renders on the GPU, our kernels loaded on that
    // device (gpu_data) and the user did not pick CPU. Otherwise AE calls SmartRender.
    const bool gpu_ready = extra->input->what_gpu != PF_GPU_Framework_NONE && extra->input->gpu_data;
    if (gpu_ready && r.popup(P_RENDER_USING, err) == RENDER_GPU_OR_CPU) {
        extra->output->flags |= PF_RenderOutputFlag_GPU_RENDER_POSSIBLE;
    }

    const int64_t frame = int64_t(std::floor(double(now) / step));
    const double fps = double(in_data->time_scale) / step;

    // Time does not depend on the layer size: decide first which frame to request.
    const flinch::TimeState time = flinch::evaluate(p_now, p_prev, frame, fps, 1, 1, mode).time;

    // Request the whole layer (Slide/Scale can reveal any area) plus whatever AE asked
    // us to render. Keep it bounded: effects that expand the buffer (e.g. a shake
    // before us) can return an empty buffer for an unbounded request.
    const double ds_y = double(in_data->downsample_y.num) / in_data->downsample_y.den;
    const PF_LRect layer_rect{0, 0, A_long(std::ceil(in_data->width * ds_x)),
                              A_long(std::ceil(in_data->height * ds_y))};
    PF_RenderRequest req = extra->input->output_request;
    req.rect = union_rect(layer_rect, extra->input->output_request.rect);
    req.preserve_rgb_of_zero_alpha = TRUE;
    req.field = PF_Field_FRAME;
    PF_CheckoutResult in_result;
    AEFX_CLR_STRUCT(in_result);
    ERR(extra->cb->checkout_layer(in_data->effect_ref, P_INPUT, kInputCheckout, &req,
                                  now + time.frame_offset * step, step, in_data->time_scale,
                                  &in_result));
    if (err) return err;

    info->state = flinch::evaluate(p_now, p_prev, frame, fps, in_result.ref_width, in_result.ref_height, mode);
    info->src_rect = in_result.result_rect;

    // AE only delivers the area with content (e.g. the box of a text under an
    // adjustment layer). Slide/Scale can move content anywhere, so the output and
    // the working canvas always cover the whole layer.
    const PF_LRect layer{0, 0, A_long(std::ceil(in_result.ref_width * ds_x)),
                         A_long(std::ceil(in_result.ref_height * ds_x))};
    const PF_LRect max = union_rect(layer, in_result.max_result_rect);
    info->canvas = union_rect(layer, in_result.result_rect);

    info->out_rect = intersect_rect(extra->input->output_request.rect, max);
    extra->output->result_rect = info->out_rect;
    extra->output->max_result_rect = max;
    extra->output->solid = FALSE;
    extra->output->pre_render_data = info.release();
    extra->output->delete_pre_render_data_func = delete_render_info;
    return err;
}

// AE buffer ↔ flinch::Image conversion (premultiplied float RGBA, 0–1).
template <class PixelT>
struct Channel;
template <>
struct Channel<PF_Pixel8> {
    static constexpr float kMax = PF_MAX_CHAN8;
    static A_u_char store(float v) { return A_u_char(std::clamp(v, 0.0f, 1.0f) * kMax + 0.5f); }
};
template <>
struct Channel<PF_Pixel16> {
    static constexpr float kMax = PF_MAX_CHAN16;
    static A_u_short store(float v) { return A_u_short(std::clamp(v, 0.0f, 1.0f) * kMax + 0.5f); }
};
template <>
struct Channel<PF_PixelFloat> {
    static constexpr float kMax = 1.0f;
    static PF_FpShort store(float v) { return v; }
};

template <class PixelT>
PixelT* row_ptr(PF_EffectWorld* w, int y) {
    return reinterpret_cast<PixelT*>(reinterpret_cast<char*>(w->data) + size_t(y) * w->rowbytes);
}

// Copies buffer `w` into `img` with its corner at (ox, oy); anything outside is dropped.
template <class PixelT>
void blit_to_image(PF_EffectWorld* w, flinch::Image& img, int ox, int oy) {
    const float inv = 1.0f / Channel<PixelT>::kMax;
    flinch::parallel_rows(w->height, [&](int y) {
        const int iy = y + oy;
        if (iy < 0 || iy >= img.height) return;
        const PixelT* row = row_ptr<PixelT>(w, y);
        for (int x = 0; x < w->width; ++x) {
            const int ix = x + ox;
            if (ix < 0 || ix >= img.width) continue;
            img.at(ix, iy) = {row[x].red * inv, row[x].green * inv, row[x].blue * inv, row[x].alpha * inv};
        }
    });
}

template <class PixelT>
void from_image(const flinch::Image& img, PF_EffectWorld* w) {
    flinch::parallel_rows(w->height, [&](int y) {
        PixelT* row = row_ptr<PixelT>(w, y);
        for (int x = 0; x < w->width; ++x) {
            const flinch::Pixel& p = img.at(x, y);
            row[x].red = Channel<PixelT>::store(p.r);
            row[x].green = Channel<PixelT>::store(p.g);
            row[x].blue = Channel<PixelT>::store(p.b);
            row[x].alpha = Channel<PixelT>::store(p.a);
        }
    });
}

// Direct input → output copy (no active twitch). Areas not covered by the input stay transparent.
template <class PixelT>
void copy_world(PF_EffectWorld* in, PF_EffectWorld* out, int dx, int dy) {
    for (int y = 0; y < out->height; ++y) {
        PixelT* dst = row_ptr<PixelT>(out, y);
        const int sy = y + dy;
        if (sy < 0 || sy >= in->height) {
            std::memset(dst, 0, sizeof(PixelT) * size_t(out->width));
            continue;
        }
        const PixelT* src = row_ptr<PixelT>(in, sy);
        for (int x = 0; x < out->width; ++x) {
            const int sx = x + dx;
            dst[x] = (sx >= 0 && sx < in->width) ? src[sx] : PixelT{};
        }
    }
}

template <class PixelT>
void clear_world(PF_EffectWorld* out) {
    for (int y = 0; y < out->height; ++y) std::memset(row_ptr<PixelT>(out, y), 0, sizeof(PixelT) * size_t(out->width));
}

// Where output pixel (0,0) sits on the canvas. The output buffer starts at the corner
// of the rect we returned in PreRender; if AE hands over a different size, fall back
// to the SDK convention (-output_origin).
flinch::Placement placement(const RenderInfo& info, const PF_EffectWorld* out, const PF_InData* in_data) {
    const PF_LRect& cv = info.canvas;
    const PF_LRect& o = info.out_rect;
    flinch::Placement pl;
    if (out->width == o.right - o.left && out->height == o.bottom - o.top) {
        pl.src_x = double(o.left - cv.left);
        pl.src_y = double(o.top - cv.top);
    } else {
        pl.src_x = double(info.src_rect.left - in_data->output_origin_x - cv.left);
        pl.src_y = double(info.src_rect.top - in_data->output_origin_y - cv.top);
    }
    pl.pixel_scale = info.pixel_scale;
    return pl;
}

template <class PixelT>
void render_world(PF_EffectWorld* in, PF_EffectWorld* out, const RenderInfo& info, const PF_InData* in_data) {
    const PF_LRect& cv = info.canvas;
    const flinch::Placement pl = placement(info, out, in_data);

    // Most frames have no twitch: copy directly without converting to float.
    if (!info.state.any_active()) {
        if (info.show_twitch_only) {
            clear_world<PixelT>(out);
        } else {
            copy_world<PixelT>(in, out, int(pl.src_x) - (info.src_rect.left - cv.left),
                               int(pl.src_y) - (info.src_rect.top - cv.top));
        }
        return;
    }

    // Everything works in canvas coords (whole layer); outside the input is transparent.
    flinch::Image canvas(cv.right - cv.left, cv.bottom - cv.top);
    blit_to_image<PixelT>(in, canvas, info.src_rect.left - cv.left, info.src_rect.top - cv.top);

    // The Scale origin comes in full-resolution layer coords.
    flinch::FrameState st = info.state;
    st.scale.origin.x -= cv.left / info.pixel_scale;
    st.scale.origin.y -= cv.top / info.pixel_scale;

    flinch::Image dst(out->width, out->height);
    flinch::render_frame(canvas, dst, st, pl, info.border);
    from_image<PixelT>(dst, out);
}

PF_Err SmartRender(PF_InData* in_data, PF_OutData* out_data, PF_SmartRenderExtra* extra) {
    PF_Err err = PF_Err_NONE, err2 = PF_Err_NONE;
    const auto* info = static_cast<const RenderInfo*>(extra->input->pre_render_data);
    if (!info) return PF_Err_BAD_CALLBACK_PARAM;

    PF_EffectWorld *input = nullptr, *output = nullptr;
    ERR(extra->cb->checkout_layer_pixels(in_data->effect_ref, kInputCheckout, &input));
    ERR(extra->cb->checkout_output(in_data->effect_ref, &output));
    if (!err && input && output) {
        AEFX_SuiteScoper<PF_WorldSuite2> world_suite(in_data, kPFWorldSuite, kPFWorldSuiteVersion2, out_data);
        PF_PixelFormat format = PF_PixelFormat_INVALID;
        ERR(world_suite->PF_GetPixelFormat(input, &format));
        if (!err) {
            switch (format) {
                case PF_PixelFormat_ARGB128: render_world<PF_PixelFloat>(input, output, *info, in_data); break;
                case PF_PixelFormat_ARGB64: render_world<PF_Pixel16>(input, output, *info, in_data); break;
                case PF_PixelFormat_ARGB32: render_world<PF_Pixel8>(input, output, *info, in_data); break;
                default: err = PF_Err_BAD_CALLBACK_PARAM; break;
            }
        }
    }
    ERR2(extra->cb->checkin_layer_pixels(in_data->effect_ref, kInputCheckout));
    return err;
}

// ---------------------------------------------------------------------------
// GPU
// ---------------------------------------------------------------------------

using GpuSuite = AEFX_SuiteScoper<PF_GPUDeviceSuite1>;

// Loads the kernels for the device AE is about to render on. On failure no
// gpu_data is returned and AE keeps rendering Flinch on the CPU.
PF_Err GpuDeviceSetup(PF_InData* in_data, PF_OutData* out_data, PF_GPUDeviceSetupExtra* extra) {
    PF_Err err = PF_Err_NONE;
    GpuSuite gpu(in_data, kPFGPUDeviceSuite, kPFGPUDeviceSuiteVersion1, out_data);
    PF_GPUDeviceInfo device;
    AEFX_CLR_STRUCT(device);
    ERR(gpu->GetDeviceInfo(in_data->effect_ref, extra->input->device_index, &device));
    if (err) return err;

    std::unique_ptr<flinch::gpu::Backend> backend;
    switch (extra->input->what_gpu) {
        case PF_GPU_Framework_CUDA:
            backend = flinch::gpu::make_cuda(device.contextPV, device.command_queuePV);
            break;
        case PF_GPU_Framework_OPENCL:
            backend = flinch::gpu::make_opencl(device.contextPV, device.devicePV, device.command_queuePV);
            break;
        case PF_GPU_Framework_METAL:
            backend = flinch::gpu::make_metal(device.devicePV, device.command_queuePV);
            break;
        default: break;
    }
    if (backend) {
        extra->output->gpu_data = backend.release();
        out_data->out_flags2 = PF_OutFlag2_SUPPORTS_GPU_RENDER_F32;
    }
    return err;
}

PF_Err GpuDeviceSetdown(PF_GPUDeviceSetdownExtra* extra) {
    delete static_cast<flinch::gpu::Backend*>(extra->input->gpu_data);
    return PF_Err_NONE;
}

// Temporary GPU worlds for one render; disposed when the render ends.
class AeScratch final : public flinch::gpu::Scratch {
public:
    AeScratch(PF_InData* in_data, const PF_GPUDeviceSuite1* suite, A_u_long device_index)
        : in_data_(in_data), suite_(suite), device_index_(device_index) {}
    ~AeScratch() override {
        for (PF_EffectWorld* w : worlds_) suite_->DisposeGPUWorld(in_data_->effect_ref, w);
    }

    bool alloc(int width, int height, flinch::gpu::Buffer& out) override {
        PF_EffectWorld* world = nullptr;
        const PF_RationalScale square{1, 1};
        if (suite_->CreateGPUWorld(in_data_->effect_ref, device_index_, width, height, square, in_data_->field,
                                   PF_PixelFormat_GPU_BGRA128, false, &world) != PF_Err_NONE)
            return false;
        worlds_.push_back(world);
        void* mem = nullptr;
        if (suite_->GetGPUWorldData(in_data_->effect_ref, world, &mem) != PF_Err_NONE) return false;
        out = {mem, width, height, int(world->rowbytes / sizeof(PF_PixelFloat))};
        return true;
    }

private:
    PF_InData* in_data_;
    const PF_GPUDeviceSuite1* suite_;
    A_u_long device_index_;
    std::vector<PF_EffectWorld*> worlds_;
};

PF_Err SmartRenderGpu(PF_InData* in_data, PF_OutData* out_data, PF_SmartRenderExtra* extra) {
    PF_Err err = PF_Err_NONE, err2 = PF_Err_NONE;
    const auto* info = static_cast<const RenderInfo*>(extra->input->pre_render_data);
    auto* backend = static_cast<flinch::gpu::Backend*>(const_cast<void*>(extra->input->gpu_data));
    if (!info || !backend) return PF_Err_BAD_CALLBACK_PARAM;

    PF_EffectWorld *input = nullptr, *output = nullptr;
    ERR(extra->cb->checkout_layer_pixels(in_data->effect_ref, kInputCheckout, &input));
    ERR(extra->cb->checkout_output(in_data->effect_ref, &output));
    if (!err && input && output) {
        AEFX_SuiteScoper<PF_WorldSuite2> world_suite(in_data, kPFWorldSuite, kPFWorldSuiteVersion2, out_data);
        GpuSuite gpu(in_data, kPFGPUDeviceSuite, kPFGPUDeviceSuiteVersion1, out_data);
        PF_PixelFormat format = PF_PixelFormat_INVALID;
        ERR(world_suite->PF_GetPixelFormat(input, &format));
        if (!err && format != PF_PixelFormat_GPU_BGRA128) err = PF_Err_UNRECOGNIZED_PARAM_TYPE;

        void *in_mem = nullptr, *out_mem = nullptr;
        ERR(gpu->GetGPUWorldData(in_data->effect_ref, input, &in_mem));
        ERR(gpu->GetGPUWorldData(in_data->effect_ref, output, &out_mem));
        if (!err) {
            const PF_LRect& cv = info->canvas;
            const flinch::Placement pl = placement(*info, output, in_data);
            flinch::gpu::Layout layout;
            layout.canvas_w = cv.right - cv.left;
            layout.canvas_h = cv.bottom - cv.top;
            layout.canvas_left = cv.left;
            layout.canvas_top = cv.top;
            layout.in_ox = info->src_rect.left - cv.left;
            layout.in_oy = info->src_rect.top - cv.top;
            layout.out_ox = pl.src_x;
            layout.out_oy = pl.src_y;
            layout.pixel_scale = info->pixel_scale;
            layout.border = int(info->border);
            layout.show_twitch_only = info->show_twitch_only;

            const int px = int(sizeof(PF_PixelFloat));
            const flinch::gpu::Buffer in_buf{in_mem, input->width, input->height, int(input->rowbytes / px)};
            const flinch::gpu::Buffer out_buf{out_mem, output->width, output->height, int(output->rowbytes / px)};
            AeScratch scratch(in_data, gpu.get(), extra->input->device_index);
            if (!flinch::gpu::render(*backend, scratch, info->state, layout, in_buf, out_buf)) {
                err = PF_Err_INTERNAL_STRUCT_DAMAGED;
            }
        }
    }
    ERR2(extra->cb->checkin_layer_pixels(in_data->effect_ref, kInputCheckout));
    return err;
}

}  // namespace

extern "C" DllExport PF_Err PluginDataEntryFunction2(PF_PluginDataPtr inPtr,
                                                     PF_PluginDataCB2 inPluginDataCallBackPtr,
                                                     SPBasicSuite* /*inSPBasicSuitePtr*/,
                                                     const char* /*inHostName*/,
                                                     const char* /*inHostVersion*/) {
    PF_Err result = PF_Err_INVALID_CALLBACK;  // the SDK macro writes to `result`
    result = PF_REGISTER_EFFECT_EXT2(inPtr, inPluginDataCallBackPtr, FLINCH_NAME, FLINCH_MATCH_NAME,
                                     FLINCH_CATEGORY, AE_RESERVED_INFO, "EffectMain", FLINCH_SUPPORT_URL);
    return result;
}

PF_Err EffectMain(PF_Cmd cmd, PF_InData* in_data, PF_OutData* out_data, PF_ParamDef* params[],
                  PF_LayerDef* /*output*/, void* extra) {
    PF_Err err = PF_Err_NONE;
    try {
        switch (cmd) {
            case PF_Cmd_ABOUT: err = About(in_data, out_data); break;
            case PF_Cmd_GLOBAL_SETUP: err = GlobalSetup(out_data); break;
            case PF_Cmd_PARAMS_SETUP: err = ParamsSetup(in_data, out_data); break;
            case PF_Cmd_UPDATE_PARAMS_UI:
            case PF_Cmd_USER_CHANGED_PARAM: err = UpdateParamsUI(in_data, params); break;
            case PF_Cmd_SMART_PRE_RENDER:
                err = PreRender(in_data, out_data, static_cast<PF_PreRenderExtra*>(extra));
                break;
            case PF_Cmd_SMART_RENDER:
                err = SmartRender(in_data, out_data, static_cast<PF_SmartRenderExtra*>(extra));
                break;
            case PF_Cmd_SMART_RENDER_GPU:
                err = SmartRenderGpu(in_data, out_data, static_cast<PF_SmartRenderExtra*>(extra));
                break;
            case PF_Cmd_GPU_DEVICE_SETUP:
                err = GpuDeviceSetup(in_data, out_data, static_cast<PF_GPUDeviceSetupExtra*>(extra));
                break;
            case PF_Cmd_GPU_DEVICE_SETDOWN:
                err = GpuDeviceSetdown(static_cast<PF_GPUDeviceSetdownExtra*>(extra));
                break;
            default: break;
        }
    } catch (PF_Err& thrown) {
        err = thrown;
    } catch (...) {
        err = PF_Err_INTERNAL_STRUCT_DAMAGED;
    }
    return err;
}
