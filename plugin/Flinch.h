// Flinch.h — declarations of the After Effects effect.
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

#define FLINCH_NAME "Flinch"
#define FLINCH_MATCH_NAME "XJFX Flinch"
#define FLINCH_CATEGORY "Flinch"
#define FLINCH_SUPPORT_URL "https://github.com/"

#define FLINCH_MAJOR_VERSION 0
#define FLINCH_MINOR_VERSION 2
#define FLINCH_BUG_VERSION 0

// Must match AE_Effect_Global_OutFlags(_2) in FlinchPiPL.r.
#define FLINCH_OUT_FLAGS                                                                              \
    (PF_OutFlag_WIDE_TIME_INPUT | PF_OutFlag_NON_PARAM_VARY | PF_OutFlag_DEEP_COLOR_AWARE |        \
     PF_OutFlag_SEND_UPDATE_PARAMS_UI)
#define FLINCH_OUT_FLAGS2                                                                             \
    (PF_OutFlag2_PARAM_GROUP_START_COLLAPSED_FLAG | PF_OutFlag2_SUPPORTS_SMART_RENDER |             \
     PF_OutFlag2_FLOAT_COLOR_AWARE | PF_OutFlag2_SUPPORTS_THREADED_RENDERING |                      \
     PF_OutFlag2_SUPPORTS_GPU_RENDER_F32)

// Every parameter as X(index name, disk ID), in the order ParamsSetup adds them
// (the order shown in the Effect Controls panel).
//
// Disk IDs identify each parameter in saved projects: never change or reuse one
// once released. They are independent of the order, so parameters can be moved.
// IDs 0–67 come from 0.1.x, where the ID was the position; later additions use 1000+.
#define FLINCH_PARAMS(X)                                                                          \
    X(P_INPUT, 0)                                                                                 \
    X(P_PATTERN, 18)                                                                              \
    X(P_RENDER_USING, 1000)                                                                       \
    X(P_AMOUNT, 1)                                                                                \
    X(P_SPEED, 2)                                                                                 \
                                                                                                  \
    X(P_BEHAVIOUR_START, 11)                                                                      \
    X(P_EASE_IN, 12)                                                                              \
    X(P_EASE_OUT, 13)                                                                             \
    X(P_BORDER, 14)                                                                               \
    X(P_SHOW_TWITCH_ONLY, 15)                                                                     \
    X(P_SEED, 16)                                                                                 \
    X(P_RAND_MIN, 17)                                                                             \
    X(P_BEHAVIOUR_END, 19)                                                                        \
                                                                                                  \
    X(P_ENABLE_START, 3)                                                                          \
    X(P_BLUR_ENABLE, 4)                                                                           \
    X(P_COLOR_ENABLE, 5)                                                                          \
    X(P_LIGHT_ENABLE, 6)                                                                          \
    X(P_SCALE_ENABLE, 7)                                                                          \
    X(P_SLIDE_ENABLE, 8)                                                                          \
    X(P_TIME_ENABLE, 9)                                                                           \
    X(P_ENABLE_END, 10)                                                                           \
                                                                                                  \
    X(P_OPERATORS_START, 20)                                                                      \
                                                                                                  \
    X(P_BLUR_START, 21)                                                                           \
    X(P_BLUR_AMOUNT, 22)                                                                          \
    X(P_BLUR_TWITCHES, 23)                                                                        \
    X(P_BLUR_TINT, 24)                                                                            \
    X(P_BLUR_BOOST, 25)                                                                           \
    X(P_BLUR_OPACITY, 26)                                                                         \
    X(P_BLUR_ASPECT, 27)                                                                          \
    X(P_BLUR_SEED, 28)                                                                            \
    X(P_BLUR_END, 29)                                                                             \
                                                                                                  \
    X(P_COLOR_START, 30)                                                                          \
    X(P_COLOR_AMOUNT, 31)                                                                         \
    X(P_COLOR_TWITCHES, 32)                                                                       \
    X(P_COLOR_COLORIZE, 33)                                                                       \
    X(P_COLOR_RANDOMIZE, 34)                                                                      \
    X(P_COLOR_SEED, 35)                                                                           \
    X(P_COLOR_END, 36)                                                                            \
                                                                                                  \
    X(P_LIGHT_START, 37)                                                                          \
    X(P_LIGHT_AMOUNT, 38)                                                                         \
    X(P_LIGHT_TWITCHES, 39)                                                                       \
    X(P_LIGHT_MODE, 40)                                                                           \
    X(P_LIGHT_SEED, 41)                                                                           \
    X(P_LIGHT_END, 42)                                                                            \
                                                                                                  \
    X(P_SCALE_START, 43)                                                                          \
    X(P_SCALE_AMOUNT, 44)                                                                         \
    X(P_SCALE_TWITCHES, 45)                                                                       \
    X(P_SCALE_ORIGIN, 46)                                                                         \
    X(P_SCALE_ORIGIN_RAND, 47)                                                                    \
    X(P_SCALE_MOTION_BLUR, 48)                                                                    \
    X(P_SCALE_SEED, 49)                                                                           \
    X(P_SCALE_END, 50)                                                                            \
                                                                                                  \
    X(P_SLIDE_START, 51)                                                                          \
    X(P_SLIDE_AMOUNT, 52)                                                                         \
    X(P_SLIDE_TWITCHES, 53)                                                                       \
    X(P_SLIDE_DIRECTION, 54)                                                                      \
    X(P_SLIDE_SPREAD, 55)                                                                         \
    X(P_SLIDE_TENDENCY, 56)                                                                       \
    X(P_SLIDE_RGB_SPLIT, 57)                                                                      \
    X(P_SLIDE_MOTION_BLUR, 58)                                                                    \
    X(P_SLIDE_SEED, 59)                                                                           \
    X(P_SLIDE_END, 60)                                                                            \
                                                                                                  \
    X(P_TIME_START, 61)                                                                           \
    X(P_TIME_AMOUNT, 62)                                                                          \
    X(P_TIME_TWITCHES, 63)                                                                        \
    X(P_TIME_DIRECTION, 64)                                                                       \
    X(P_TIME_SEED, 65)                                                                            \
    X(P_TIME_END, 66)                                                                             \
                                                                                                  \
    X(P_OPERATORS_END, 67)

// Parameter indices, in display order.
enum {
#define FLINCH_PARAM_INDEX(name, id) name,
    FLINCH_PARAMS(FLINCH_PARAM_INDEX)
#undef FLINCH_PARAM_INDEX
    P_NUM_PARAMS
};

// "Render Using" choices.
enum { RENDER_GPU_OR_CPU = 1, RENDER_CPU = 2 };

inline A_long disk_id(int param_index) {
    static constexpr A_long kIds[] = {
#define FLINCH_PARAM_ID(name, id) id,
        FLINCH_PARAMS(FLINCH_PARAM_ID)
#undef FLINCH_PARAM_ID
    };
    static_assert(sizeof(kIds) / sizeof(kIds[0]) == P_NUM_PARAMS, "one disk ID per parameter");
    return kIds[param_index];
}

extern "C" {
DllExport PF_Err EffectMain(PF_Cmd cmd, PF_InData* in_data, PF_OutData* out_data,
                            PF_ParamDef* params[], PF_LayerDef* output, void* extra);
}
