# Compiles a PiPL resource (.r) into .rc, the same way the AE SDK projects do:
#   cl /EP → PiPLTool → cl /EP
# Usage: cmake -DCL=... -DPIPLTOOL=... -DSDK_HEADERS=... -DINPUT=x.r -DOUT_DIR=... -DOUTPUT=x.rc -P pipl.cmake
get_filename_component(name "${INPUT}" NAME_WE)
set(rr "${OUT_DIR}/${name}.rr")
set(rrc "${OUT_DIR}/${name}.rrc")

execute_process(COMMAND "${CL}" /nologo /I "${SDK_HEADERS}" /EP "${INPUT}"
                OUTPUT_FILE "${rr}" RESULT_VARIABLE res)
if(res)
  message(FATAL_ERROR "PiPL preprocessing failed (${res})")
endif()

# PiPLTool cannot handle paths with spaces: run it in OUT_DIR with relative names.
execute_process(COMMAND "${PIPLTOOL}" "${name}.rr" "${name}.rrc"
                WORKING_DIRECTORY "${OUT_DIR}" RESULT_VARIABLE res)
if(res)
  message(FATAL_ERROR "PiPLTool failed (${res})")
endif()

execute_process(COMMAND "${CL}" /nologo /D MSWindows /EP "${rrc}"
                OUTPUT_FILE "${OUTPUT}" RESULT_VARIABLE res)
if(res)
  message(FATAL_ERROR ".rc generation failed (${res})")
endif()
