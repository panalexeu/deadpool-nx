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
