#include "AEConfig.h"
#include "AE_EffectVers.h"

#ifndef AE_OS_WIN
	#include <AE_General.r>
#endif

resource 'PiPL' (16000) {
	{
		Kind {
			AEEffect
		},
		Name {
			"Flinch"
		},
		Category {
			"Flinch"
		},
#ifdef AE_OS_WIN
    #if defined(AE_PROC_INTELx64)
		CodeWin64X86 {"EffectMain"},
    #elif defined(AE_PROC_ARM64)
		CodeWinARM64 {"EffectMain"},
    #endif
#elif defined(AE_OS_MAC)
		CodeMacIntel64 {"EffectMain"},
		CodeMacARM64 {"EffectMain"},
#endif
		AE_PiPL_Version {
			2,
			0
		},
		AE_Effect_Spec_Version {
			PF_PLUG_IN_VERSION,
			PF_PLUG_IN_SUBVERS
		},
		AE_Effect_Version {
			32769	/* PF_VERSION(0, 1, 0, PF_Stage_DEVELOP, 1) */
		},
		AE_Effect_Info_Flags {
			0
		},
		/* WIDE_TIME_INPUT | NON_PARAM_VARY | DEEP_COLOR_AWARE | SEND_UPDATE_PARAMS_UI */
		AE_Effect_Global_OutFlags {
			0x06000006
		},
		/* GROUP_START_COLLAPSED | SMART_RENDER | FLOAT_COLOR_AWARE | THREADED_RENDERING */
		AE_Effect_Global_OutFlags_2 {
			0x08001408
		},
		AE_Effect_Match_Name {
			"XJFX Flinch"
		},
		AE_Reserved_Info {
			0
		},
		AE_Effect_Support_URL {
			"https://github.com/"
		}
	}
};
