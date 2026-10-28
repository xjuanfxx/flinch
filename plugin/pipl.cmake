# Compila un recurso PiPL (.r) a .rc, igual que los proyectos del SDK de AE:
#   cl /EP → PiPLTool → cl /EP
# Uso: cmake -DCL=... -DPIPLTOOL=... -DSDK_HEADERS=... -DINPUT=x.r -DOUT_DIR=... -DOUTPUT=x.rc -P pipl.cmake
get_filename_component(name "${INPUT}" NAME_WE)
set(rr "${OUT_DIR}/${name}.rr")
set(rrc "${OUT_DIR}/${name}.rrc")

execute_process(COMMAND "${CL}" /nologo /I "${SDK_HEADERS}" /EP "${INPUT}"
                OUTPUT_FILE "${rr}" RESULT_VARIABLE res)
if(res)
  message(FATAL_ERROR "Preprocesado de la PiPL falló (${res})")
endif()

# PiPLTool no admite rutas con espacios: se ejecuta en OUT_DIR con nombres relativos.
execute_process(COMMAND "${PIPLTOOL}" "${name}.rr" "${name}.rrc"
                WORKING_DIRECTORY "${OUT_DIR}" RESULT_VARIABLE res)
if(res)
  message(FATAL_ERROR "PiPLTool falló (${res})")
endif()

execute_process(COMMAND "${CL}" /nologo /D MSWindows /EP "${rrc}"
                OUTPUT_FILE "${OUTPUT}" RESULT_VARIABLE res)
if(res)
  message(FATAL_ERROR "Generación del .rc falló (${res})")
endif()
