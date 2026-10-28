// TwitchRework.h — declaraciones del efecto de After Effects.
#pragma once

#define PF_DEEP_COLOR_AWARE 1

#include "AEConfig.h"
#ifdef AE_OS_WIN
    #include <Windows.h>
#endif
#include "entry.h"
#include "AE_Effect.h"
#include "AE_EffectCB.h"
#include "AE_EffectCBSuites.h"
#include "AE_Macros.h"
#include "AEFX_SuiteHandlerTemplate.h"
#include "AEGP_SuiteHandler.h"
#include "Param_Utils.h"

#define TWR_NAME "Twitch Rework"
#define TWR_MATCH_NAME "XJFX TwitchRework"
#define TWR_CATEGORY "Twitch Rework"
#define TWR_SUPPORT_URL "https://github.com/"

#define TWR_MAJOR_VERSION 0
#define TWR_MINOR_VERSION 1
#define TWR_BUG_VERSION 0

// Deben coincidir con AE_Effect_Global_OutFlags(_2) en TwitchReworkPiPL.r.
#define TWR_OUT_FLAGS                                                                              \
    (PF_OutFlag_WIDE_TIME_INPUT | PF_OutFlag_NON_PARAM_VARY | PF_OutFlag_DEEP_COLOR_AWARE |        \
     PF_OutFlag_SEND_UPDATE_PARAMS_UI)
#define TWR_OUT_FLAGS2                                                                             \
    (PF_OutFlag2_PARAM_GROUP_START_COLLAPSED_FLAG | PF_OutFlag2_SUPPORTS_SMART_RENDER |             \
     PF_OutFlag2_FLOAT_COLOR_AWARE | PF_OutFlag2_SUPPORTS_THREADED_RENDERING)

// Índices de parámetros, en el orden en que se añaden en ParamsSetup.
// La disposición sigue la de Twitch: Enable / Behaviour / Operator Controls.
enum {
    P_INPUT = 0,
    P_AMOUNT,
    P_SPEED,

    P_ENABLE_START,
    P_BLUR_ENABLE,
    P_COLOR_ENABLE,
    P_LIGHT_ENABLE,
    P_SCALE_ENABLE,
    P_SLIDE_ENABLE,
    P_TIME_ENABLE,
    P_ENABLE_END,

    P_BEHAVIOUR_START,
    P_EASE_IN,
    P_EASE_OUT,
    P_BORDER,
    P_SHOW_TWITCH_ONLY,
    P_SEED,
    P_RAND_MIN,
    P_PATTERN,
    P_BEHAVIOUR_END,

    P_OPERATORS_START,

    P_BLUR_START,
    P_BLUR_AMOUNT,
    P_BLUR_TWITCHES,
    P_BLUR_TINT,
    P_BLUR_BOOST,
    P_BLUR_OPACITY,
    P_BLUR_ASPECT,
    P_BLUR_SEED,
    P_BLUR_END,

    P_COLOR_START,
    P_COLOR_AMOUNT,
    P_COLOR_TWITCHES,
    P_COLOR_COLORIZE,
    P_COLOR_RANDOMIZE,
    P_COLOR_SEED,
    P_COLOR_END,

    P_LIGHT_START,
    P_LIGHT_AMOUNT,
    P_LIGHT_TWITCHES,
    P_LIGHT_MODE,
    P_LIGHT_SEED,
    P_LIGHT_END,

    P_SCALE_START,
    P_SCALE_AMOUNT,
    P_SCALE_TWITCHES,
    P_SCALE_ORIGIN,
    P_SCALE_ORIGIN_RAND,
    P_SCALE_MOTION_BLUR,
    P_SCALE_SEED,
    P_SCALE_END,

    P_SLIDE_START,
    P_SLIDE_AMOUNT,
    P_SLIDE_TWITCHES,
    P_SLIDE_DIRECTION,
    P_SLIDE_SPREAD,
    P_SLIDE_TENDENCY,
    P_SLIDE_RGB_SPLIT,
    P_SLIDE_MOTION_BLUR,
    P_SLIDE_SEED,
    P_SLIDE_END,

    P_TIME_START,
    P_TIME_AMOUNT,
    P_TIME_TWITCHES,
    P_TIME_DIRECTION,
    P_TIME_SEED,
    P_TIME_END,

    P_OPERATORS_END,

    P_NUM_PARAMS
};

// Disk IDs: identifican cada parámetro en los proyectos guardados.
// No cambiar ni reutilizar una vez publicado.
inline A_long disk_id(int param_index) { return param_index; }

extern "C" {
DllExport PF_Err EffectMain(PF_Cmd cmd, PF_InData* in_data, PF_OutData* out_data,
                            PF_ParamDef* params[], PF_LayerDef* output, void* extra);
}
