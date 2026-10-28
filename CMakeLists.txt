cmake_minimum_required(VERSION 3.20)

# macOS: binario universal (Apple Silicon + Intel). Debe fijarse antes de project().
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
  set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "Arquitecturas de macOS")
  set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "macOS mínimo")
endif()

project(TwitchRework CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(MSVC)
  add_compile_options(/W4 /utf-8)
endif()

add_library(twitch_core INTERFACE)
target_include_directories(twitch_core INTERFACE core/include)

add_executable(twitch_curves tools/twitch_curves.cpp)
target_link_libraries(twitch_curves PRIVATE twitch_core)

add_executable(bench_render tools/bench_render.cpp)
target_link_libraries(bench_render PRIVATE twitch_core)

enable_testing()
add_executable(test_core tests/test_core.cpp)
target_link_libraries(test_core PRIVATE twitch_core)
add_test(NAME core COMMAND test_core)

add_executable(test_render tests/test_render.cpp)
target_link_libraries(test_render PRIVATE twitch_core)
add_test(NAME render COMMAND test_render)

# --- Plugin de After Effects ------------------------------------------------
set(AE_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ae_sdk"
    CACHE PATH "Carpeta con Headers/, Util/ y Resources/ del After Effects SDK")

if(NOT EXISTS "${AE_SDK_DIR}/Headers/AE_Effect.h")
  message(STATUS "AE SDK no encontrado en ${AE_SDK_DIR}: se omite el plugin")
  return()
endif()

set(pipl_src "${CMAKE_CURRENT_SOURCE_DIR}/plugin/TwitchReworkPiPL.r")
set(plugin_sources
  plugin/TwitchRework.cpp
  "${AE_SDK_DIR}/Util/AEGP_SuiteHandler.cpp"
  "${AE_SDK_DIR}/Util/MissingSuiteError.cpp")

if(WIN32)
  set(pipl_rc "${CMAKE_CURRENT_BINARY_DIR}/TwitchReworkPiPL.rc")
  add_custom_command(
    OUTPUT "${pipl_rc}"
    COMMAND "${CMAKE_COMMAND}"
            "-DCL=${CMAKE_CXX_COMPILER}"
            "-DPIPLTOOL=${AE_SDK_DIR}/Resources/PiPLTool.exe"
            "-DSDK_HEADERS=${AE_SDK_DIR}/Headers"
            "-DINPUT=${pipl_src}"
            "-DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR}"
            "-DOUTPUT=${pipl_rc}"
            -P "${CMAKE_CURRENT_SOURCE_DIR}/plugin/pipl.cmake"
    DEPENDS "${pipl_src}" "${CMAKE_CURRENT_SOURCE_DIR}/plugin/pipl.cmake"
    COMMENT "Compilando la PiPL"
    VERBATIM)

  add_library(TwitchRework MODULE ${plugin_sources} "${pipl_rc}")
  set_target_properties(TwitchRework PROPERTIES SUFFIX ".aex" PREFIX "")
  target_compile_definitions(TwitchRework PRIVATE MSWindows WIN32 _WINDOWS NOMINMAX)

elseif(APPLE)
  # La PiPL va como recurso (data fork) en Contents/Resources/TwitchRework.rsrc.
  execute_process(COMMAND xcrun --show-sdk-path OUTPUT_VARIABLE macos_sdk
                  OUTPUT_STRIP_TRAILING_WHITESPACE)
  set(pipl_rsrc "${CMAKE_CURRENT_BINARY_DIR}/TwitchRework.rsrc")
  add_custom_command(
    OUTPUT "${pipl_rsrc}"
    COMMAND xcrun Rez -o "${pipl_rsrc}" -define __MACH__ -useDF -script Roman
            -i "${AE_SDK_DIR}/Headers" -i "${AE_SDK_DIR}/Resources"
            -isysroot "${macos_sdk}" "${pipl_src}"
    DEPENDS "${pipl_src}"
    COMMENT "Compilando la PiPL (Rez)"
    VERBATIM)
  set_source_files_properties("${pipl_rsrc}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)

  add_library(TwitchRework MODULE ${plugin_sources} "${pipl_rsrc}")
  set_target_properties(TwitchRework PROPERTIES
    BUNDLE TRUE
    BUNDLE_EXTENSION plugin
    OUTPUT_NAME TwitchRework
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/plugin/Info.plist.in"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.xjfx.TwitchRework"
    CXX_VISIBILITY_PRESET hidden)
else()
  message(STATUS "Plataforma sin soporte para el plugin de AE")
  return()
endif()

target_include_directories(TwitchRework PRIVATE
  "${AE_SDK_DIR}/Headers" "${AE_SDK_DIR}/Headers/SP"
  "${AE_SDK_DIR}/Resources" "${AE_SDK_DIR}/Util")
target_link_libraries(TwitchRework PRIVATE twitch_core)
