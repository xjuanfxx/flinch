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

# --- GPU kernels and backends ------------------------------------------------
# One kernel source (plugin/gpu/flinch_kernels.inc) feeds three backends:
# OpenCL and Metal compile it at runtime from embedded text; CUDA embeds PTX
# built here with nvcc (optional: without nvcc, CUDA projects render on the CPU).
set(gpu_dir "${CMAKE_CURRENT_SOURCE_DIR}/plugin/gpu")
set(gpu_gen "${CMAKE_CURRENT_BINARY_DIR}/gpu")
file(MAKE_DIRECTORY "${gpu_gen}")
set(kernel_inputs "${gpu_dir}/flinch_gpu_types.h" "${gpu_dir}/flinch_kernels.inc")

function(flinch_embed output name)
  list(JOIN ARGN "|" files)
  add_custom_command(
    OUTPUT "${output}"
    COMMAND "${CMAKE_COMMAND}" "-DOUTPUT=${output}" "-DNAME=${name}" "-DFILES=${files}"
            -P "${gpu_dir}/embed.cmake"
    DEPENDS ${ARGN} "${gpu_dir}/embed.cmake"
    COMMENT "Embedding ${name}"
    VERBATIM)
endfunction()

set(gpu_sources "${gpu_dir}/FlinchGpuCuda.cpp" "${gpu_dir}/FlinchGpuOpenCL.cpp")
set(gpu_defs "")
if(WIN32)
  flinch_embed("${gpu_gen}/flinch_kernels_opencl.h" kFlinchOpenCLSource
               "${gpu_dir}/prelude_opencl.cl" ${kernel_inputs})
  list(APPEND gpu_sources "${gpu_gen}/flinch_kernels_opencl.h" "${gpu_dir}/FlinchGpuMetalStub.cpp")

  find_program(FLINCH_NVCC nvcc
               HINTS "$ENV{CUDA_PATH}/bin" "$ENV{USERPROFILE}/Tools/cuda-12.8/bin")
  if(FLINCH_NVCC)
    get_filename_component(host_cl_dir "${CMAKE_CXX_COMPILER}" DIRECTORY)
    add_custom_command(
      OUTPUT "${gpu_gen}/flinch_kernels.ptx"
      COMMAND "${FLINCH_NVCC}" -ptx -arch=compute_50 -Wno-deprecated-gpu-targets
              -allow-unsupported-compiler -ccbin "${host_cl_dir}" -I "${gpu_dir}"
              "${gpu_dir}/flinch_kernels.cu" -o "${gpu_gen}/flinch_kernels.ptx"
      DEPENDS "${gpu_dir}/flinch_kernels.cu" ${kernel_inputs}
      COMMENT "Compiling CUDA kernels to PTX"
      VERBATIM)
    flinch_embed("${gpu_gen}/flinch_kernels_ptx.h" kFlinchPtx "${gpu_gen}/flinch_kernels.ptx")
    list(APPEND gpu_sources "${gpu_gen}/flinch_kernels_ptx.h")
    list(APPEND gpu_defs FLINCH_HAS_CUDA)
    message(STATUS "CUDA kernels: ${FLINCH_NVCC}")
  else()
    message(STATUS "nvcc not found: CUDA projects will render Flinch on the CPU")
  endif()
elseif(APPLE)
  enable_language(OBJCXX)
  flinch_embed("${gpu_gen}/flinch_kernels_metal.h" kFlinchMetalSource
               "${gpu_dir}/prelude_metal.metal" ${kernel_inputs})
  list(APPEND gpu_sources "${gpu_dir}/FlinchGpuMetal.mm" "${gpu_gen}/flinch_kernels_metal.h")
  set_source_files_properties("${gpu_dir}/FlinchGpuMetal.mm" PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
endif()

add_library(flinch_gpu STATIC ${gpu_sources})
target_include_directories(flinch_gpu PUBLIC "${gpu_dir}" "${gpu_gen}")
target_compile_definitions(flinch_gpu PRIVATE ${gpu_defs})
target_link_libraries(flinch_gpu PUBLIC flinch_core)
if(WIN32)
  target_compile_definitions(flinch_gpu PUBLIC NOMINMAX PRIVATE WIN32_LEAN_AND_MEAN)
elseif(APPLE)
  target_link_libraries(flinch_gpu PUBLIC "-framework Metal" "-framework Foundation")
endif()

# Runs the GPU kernels on this machine (CUDA and OpenCL) and compares them with
# the CPU renderer. Windows only; not part of ctest because CI has no GPU.
if(WIN32)
  add_executable(gpu_check tools/gpu_check.cpp)
  target_link_libraries(gpu_check PRIVATE flinch_gpu)
endif()

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
target_link_libraries(Flinch PRIVATE flinch_gpu)
