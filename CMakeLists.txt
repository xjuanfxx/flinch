cmake_minimum_required(VERSION 3.20)

# macOS: universal binary (Apple Silicon + Intel). Must be set before project().
if(CMAKE_HOST_SYSTEM_NAME STREQUAL "Darwin")
  set(CMAKE_OSX_ARCHITECTURES "arm64;x86_64" CACHE STRING "macOS architectures")
  set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS version")
endif()

project(Flinch CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(MSVC)
  add_compile_options(/W4 /utf-8)
endif()

add_library(flinch_core INTERFACE)
target_include_directories(flinch_core INTERFACE core/include)

add_executable(flinch_curves tools/flinch_curves.cpp)
target_link_libraries(flinch_curves PRIVATE flinch_core)

add_executable(bench_render tools/bench_render.cpp)
target_link_libraries(bench_render PRIVATE flinch_core)

enable_testing()
add_executable(test_core tests/test_core.cpp)
target_link_libraries(test_core PRIVATE flinch_core)
add_test(NAME core COMMAND test_core)

add_executable(test_render tests/test_render.cpp)
target_link_libraries(test_render PRIVATE flinch_core)
add_test(NAME render COMMAND test_render)

# --- After Effects plugin --------------------------------------------------
set(AE_SDK_DIR "${CMAKE_CURRENT_SOURCE_DIR}/third_party/ae_sdk"
    CACHE PATH "Folder with the After Effects SDK Headers/, Util/ and Resources/")

if(NOT EXISTS "${AE_SDK_DIR}/Headers/AE_Effect.h")
  message(STATUS "AE SDK not found in ${AE_SDK_DIR}: skipping the plugin")
  return()
endif()

set(pipl_src "${CMAKE_CURRENT_SOURCE_DIR}/plugin/FlinchPiPL.r")
set(plugin_sources
  plugin/Flinch.cpp
  "${AE_SDK_DIR}/Util/AEGP_SuiteHandler.cpp"
  "${AE_SDK_DIR}/Util/MissingSuiteError.cpp")

if(WIN32)
  set(pipl_rc "${CMAKE_CURRENT_BINARY_DIR}/FlinchPiPL.rc")
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
    COMMENT "Compiling the PiPL"
    VERBATIM)

  add_library(Flinch MODULE ${plugin_sources} "${pipl_rc}")
  set_target_properties(Flinch PROPERTIES SUFFIX ".aex" PREFIX "")
  target_compile_definitions(Flinch PRIVATE MSWindows WIN32 _WINDOWS NOMINMAX)

elseif(APPLE)
  # The PiPL ships as a data-fork resource in Contents/Resources/Flinch.rsrc.
  execute_process(COMMAND xcrun --show-sdk-path OUTPUT_VARIABLE macos_sdk
                  OUTPUT_STRIP_TRAILING_WHITESPACE)
  set(pipl_rsrc "${CMAKE_CURRENT_BINARY_DIR}/Flinch.rsrc")
  add_custom_command(
    OUTPUT "${pipl_rsrc}"
    COMMAND xcrun Rez -o "${pipl_rsrc}" -define __MACH__ -useDF -script Roman
            -i "${AE_SDK_DIR}/Headers" -i "${AE_SDK_DIR}/Resources"
            -isysroot "${macos_sdk}" "${pipl_src}"
    DEPENDS "${pipl_src}"
    COMMENT "Compiling the PiPL (Rez)"
    VERBATIM)
  set_source_files_properties("${pipl_rsrc}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)

  add_library(Flinch MODULE ${plugin_sources} "${pipl_rsrc}")
  set_target_properties(Flinch PROPERTIES
    BUNDLE TRUE
    BUNDLE_EXTENSION plugin
    OUTPUT_NAME Flinch
    MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/plugin/Info.plist.in"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.xjfx.Flinch"
    CXX_VISIBILITY_PRESET hidden)
else()
  message(STATUS "Platform not supported for the AE plugin")
  return()
endif()

target_include_directories(Flinch PRIVATE
  "${AE_SDK_DIR}/Headers" "${AE_SDK_DIR}/Headers/SP"
  "${AE_SDK_DIR}/Resources" "${AE_SDK_DIR}/Util")
target_link_libraries(Flinch PRIVATE flinch_core)
