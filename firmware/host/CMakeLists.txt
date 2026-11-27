# host/CMakeLists.txt — Tier-1 native desktop build (Linux + SDL2 + LVGL).
#
# Completely independent of the ESP-IDF build: ESP-IDF consumes
# ../CMakeLists.txt, this file is configured on its own into ../build-host.
# `just build-desktop` drives it; `just build-s3` is untouched.
#
# What is listed here is only the simulator's own files (its backends and main).
# The ui/, model/ and hal/ boundaries come from ../libs.txt through
# ../sources.cmake — the same boundaries the IDF build registers as components.

cmake_minimum_required(VERSION 3.16)
project(dumbphone-desktop C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Debug)
endif()

# Optional flush instrumentation: counts the bytes/areas pushed through
# hal_display_blit* and prints a summary at exit. Off by default — the
# normal build is untouched (see `just build-desktop-stats`).
option(FLUSH_STATS "instrument LVGL flushes with byte/area counters" OFF)

add_compile_options(-Wall -Wextra -Wno-unused-parameter)

# ---- dependencies ---------------------------------------------------------
# Dev headers/libs live in a user prefix on this machine (RPMs extracted with
# rpm2cpio by hp-nobara; no root needed). An explicit override wins:
#   cmake -DDEPS_PREFIX=/some/prefix ..
set(DEPS_PREFIX "$ENV{HOME}/dev-deps" CACHE PATH "user-space prefix with SDL2/freetype dev files")

find_path(SDL2_INCLUDE_DIR SDL2/SDL.h
          HINTS ${DEPS_PREFIX}/usr/include
          REQUIRED)
find_library(SDL2_LIBRARY NAMES SDL2
             HINTS ${DEPS_PREFIX}/usr/lib64 ${DEPS_PREFIX}/usr/lib
             REQUIRED)

# libjpeg (turbo): used by the browser frame server.
find_path(JPEG_INCLUDE_DIR jpeglib.h
          HINTS ${DEPS_PREFIX}/usr/include
          REQUIRED)
find_library(JPEG_LIBRARY NAMES jpeg
             HINTS ${DEPS_PREFIX}/usr/lib64 ${DEPS_PREFIX}/usr/lib /usr/lib64
             REQUIRED)

# ---- LVGL (vendored, pinned tag v9.4.0) -----------------------------------
# One checkout for every build: this subdirectory and the IDF component both
# point at vendor/lvgl (issue #17). The device build uses the same tree through
# its own component wrapper.
set(LVGL_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../vendor/lvgl")
if(NOT EXISTS "${LVGL_DIR}/CMakeLists.txt")
    message(FATAL_ERROR "LVGL not found at ${LVGL_DIR}. Run `just vendor` or:\n"
                        "  git clone --depth 1 --branch v9.4.0 https://github.com/lvgl/lvgl.git vendor/lvgl")
endif()

set(LV_CONF_PATH "${CMAKE_CURRENT_SOURCE_DIR}/lv_conf.h" CACHE STRING "" FORCE)
# Keep the skeleton lean: no examples/demos/vector graphics in the host build.
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG_INTERNAL OFF CACHE BOOL "" FORCE)
add_subdirectory(${LVGL_DIR} ${CMAKE_BINARY_DIR}/lvgl)

# LVGL is vendored at a pinned tag and is not ours to fix. It carries one
# -Wunused-but-set-variable in lv_buttonmatrix.c; keep the build log quiet so
# a warning in OUR code can never hide in third-party noise.
target_compile_options(lvgl PRIVATE -Wno-unused-but-set-variable)

# ---- the boundaries --------------------------------------------------------
set(FW_DIR "${CMAKE_CURRENT_SOURCE_DIR}/..")
include(${FW_DIR}/sources.cmake)
add_subdirectory(${FW_DIR}/ui ${CMAKE_BINARY_DIR}/dp_ui)
add_subdirectory(${FW_DIR}/model ${CMAKE_BINARY_DIR}/dp_model)
add_subdirectory(${FW_DIR}/hal ${CMAKE_BINARY_DIR}/dp_hal)

# The simulator's side of the port seam (ui/ui_port.h). The device side lives in
# hal/esp32s3/ and is compiled only by the IDF build.
add_library(dp_port STATIC src/ui_port_host.c)
target_include_directories(dp_port PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include ${FW_DIR})
target_link_libraries(dp_port PUBLIC lvgl dp_hal pthread)
# Font assets are resolved at runtime; bake the source-tree path in. A device
# build has no filesystem: faces come from the copy embedded in the image.
target_compile_definitions(dp_port PRIVATE
    FONT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts")

# ---- the simulator --------------------------------------------------------
add_executable(dumbphone-desktop
    src/main.c
    src/hal_sdl_display.c
    src/hal_sdl_input.c
    src/bezel.c
    src/frame_server.c
    src/chat_source_json.c
    src/hal_stubs.c
)

target_include_directories(dumbphone-desktop PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${SDL2_INCLUDE_DIR}
    ${JPEG_INCLUDE_DIR}
)

target_compile_definitions(dumbphone-desktop PRIVATE
    CHAT_DATA_PATH="${CMAKE_CURRENT_SOURCE_DIR}/data/chats.json"
    CHAT_SAMPLE_PATH="${FW_DIR}/fixtures/chats.sample.json"
)

# Optional flush instrumentation (see `just build-desktop-stats`): counts
# the bytes/areas pushed through hal_display_blit*, summary at exit.
if(FLUSH_STATS)
    target_compile_definitions(dumbphone-desktop PRIVATE UI_FLUSH_STATS=1)
    target_compile_definitions(dp_port PRIVATE UI_FLUSH_STATS=1)
endif()

target_link_libraries(dumbphone-desktop PRIVATE
    dp_ui dp_model dp_hal dp_port
    ${SDL2_LIBRARY}
    ${JPEG_LIBRARY}
    pthread
    m
)

# ---- host unit tests (spec §16.1, issue #7) ----------------------------
# Display-free tests for the pure UI modules: the data-text direction rule
# and the geometry derivations. No framework — main() + checks + a
# PASS/FAIL summary; exit 0 iff everything passes. `just test-host` runs it,
# `just check` includes it in the local gate. Same boundaries, no file list of
# its own: the linker pulls only the objects the tests reference.
add_executable(test-host ${FW_DIR}/test/test_host.c src/hal_stubs.c)
target_link_libraries(test-host PRIVATE dp_ui dp_model dp_hal dp_port)
