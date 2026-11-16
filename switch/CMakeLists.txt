# deadpool-nx - the native (deko3d) Switch build. From gears2-nx's gow2/CMakeLists.txt (GPL-3.0).
#
# Configure (tools/switch/build-native.sh does this in Docker):
#   cmake -S switch -B out/build/switch-native -G Ninja -DCMAKE_BUILD_TYPE=Release \
#     -DCMAKE_TOOLCHAIN_FILE=tools/switch/switch-devkitA64.cmake -DREXGLUE_SWITCH_DEKO3D=ON \
#     -DREXSDK_DIR=<rexglue-sdk-nx> -DDEADPOOL_CODEGEN_DIR=out/codegen-nx
cmake_minimum_required(VERSION 3.25)
project(deadpool LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(cmake/deadpool_switch.cmake)

if(NOT REXGLUE_SWITCH_DEKO3D)
    message(FATAL_ERROR "deadpool: the native build is deko3d only (-DREXGLUE_SWITCH_DEKO3D=ON)")
endif()

set(DEADPOOL_SHADER_TOOLS "${CMAKE_CURRENT_SOURCE_DIR}/../tools/switch/shaders")

add_executable(deadpool
    src/main.cpp
    src/deadpool_gpu.cpp
    src/deadpool_shaders.cpp
    src/deadpool_cpu.cpp
    src/deadpool_guards.cpp
    src/deadpool_game_patches.cpp
    src/deadpool_perf_hooks.cpp
    src/deadpool_pgo.cpp
    ${DEADPOOL_SHADER_TOOLS}/ds_shader_library.cpp
)
deadpool_setup_target(deadpool)

# The guest's mtmsrd regions as one global mutex instead of per thread (gears2-nx's option, off).
option(DEADPOOL_MSR_GLOBAL "The mtmsrd regions as one global mutex by default" OFF)
if(DEADPOOL_MSR_GLOBAL)
    set_source_files_properties(src/deadpool_cpu.cpp PROPERTIES COMPILE_DEFINITIONS DEADPOOL_MSR_GLOBAL_DEFAULT=1)
endif()

rexglue_switch_add_deko3d(deadpool)
target_compile_definitions(deadpool PRIVATE DEADPOOL_DEKO3D=1)
target_link_libraries(deadpool PRIVATE mspack)
target_include_directories(deadpool PRIVATE ${DEADPOOL_SHADER_TOOLS} ${REXSDK_DIR}/thirdparty/xxHash)

# Profile-guided optimisation (docs: README "Profile-guided build"). PGO=generate: an instrumented NRO that streams its
# branch counts to pgo/ on the SD card (src/deadpool_pgo.cpp); tools/switch/pgo_merge.sh turns them into .gcda files in
# DEADPOOL_PGO_DIR; PGO=use builds with them. The recompiled game, this app and the SDK runtime (renderer, GPU ring,
# kernel, audio glue) are profiled; -fprofile-prefix-path makes the profile's names independent of the build folder.
set(DEADPOOL_PGO "" CACHE STRING "Profile-guided optimisation: empty, generate or use")
set(DEADPOOL_PGO_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../pgo" CACHE PATH "Folder of the PGO profile (.gcda files)")
if(DEADPOOL_PGO)
    get_filename_component(_deadpool_pgo_dir "${DEADPOOL_PGO_DIR}" ABSOLUTE)
    if(DEADPOOL_PGO STREQUAL "generate")
        set(_deadpool_pgo "-fprofile-generate=${_deadpool_pgo_dir}" "-fprofile-prefix-path=${CMAKE_BINARY_DIR}"
                          -fno-profile-values -fprofile-update=single -fprofile-info-section=gcov_info)
        target_compile_definitions(deadpool PRIVATE DEADPOOL_PGO_GENERATE=1)
        target_link_options(deadpool PRIVATE "-fprofile-generate=${_deadpool_pgo_dir}")
    elseif(DEADPOOL_PGO STREQUAL "use")
        file(GLOB _deadpool_pgo_gcda "${_deadpool_pgo_dir}/*.gcda")
        if(NOT _deadpool_pgo_gcda)
            message(FATAL_ERROR "DEADPOOL_PGO=use: no profile in ${_deadpool_pgo_dir} (tools/switch/pgo_merge.sh)")
        endif()
        set(_deadpool_pgo "-fprofile-use=${_deadpool_pgo_dir}" "-fprofile-prefix-path=${CMAKE_BINARY_DIR}"
                          -fprofile-correction -fno-profile-values -fprofile-partial-training -Wno-missing-profile
                          -Wno-coverage-mismatch)
    else()
        message(FATAL_ERROR "DEADPOOL_PGO must be empty, generate or use (got ${DEADPOOL_PGO})")
    endif()
    foreach(_t deadpool deadpool_recomp rexruntime rexcore rexaudio rexfilesystem libavcodec libavutil)
        if(TARGET ${_t})
            target_compile_options(${_t} PRIVATE "$<$<COMPILE_LANGUAGE:C,CXX>:${_deadpool_pgo}>")
        endif()
    endforeach()
    message(STATUS "deadpool: PGO ${DEADPOOL_PGO} (${_deadpool_pgo_dir})")
endif()

set(DEADPOOL_VERSION "0.1")
execute_process(COMMAND git rev-parse --short=10 HEAD WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
                OUTPUT_VARIABLE DEADPOOL_COMMIT OUTPUT_STRIP_TRAILING_WHITESPACE ERROR_QUIET)
if(NOT DEADPOOL_COMMIT)
    set(DEADPOOL_COMMIT "unknown")
endif()
target_compile_definitions(deadpool PRIVATE DEADPOOL_VERSION="${DEADPOOL_VERSION}"
                                               DEADPOOL_COMMIT="${DEADPOOL_COMMIT}")

if(REXGLUE_PLATFORM_SWITCH)
    rexglue_switch_add_nro(deadpool
        NAME "Deadpool"
        AUTHOR "alexeu"
        VERSION "${DEADPOOL_VERSION}"
        ICON "${CMAKE_CURRENT_SOURCE_DIR}/icon.jpg")  # 256x256 baseline JPEG
endif()
