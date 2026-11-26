# host/CMakeLists.txt — Tier-1 native desktop build (Linux + SDL2 + LVGL).
#
# Completely independent of the ESP-IDF build: ESP-IDF consumes
# ../CMakeLists.txt, this file is configured on its own into ../build-host.
# `just build-desktop` drives it; `just build-s3` is untouched.

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

# ---- the simulator --------------------------------------------------------
set(FW_DIR "${CMAKE_CURRENT_SOURCE_DIR}/..")

add_executable(dumbphone-desktop
    src/main.c
    src/hal_sdl_display.c
    src/hal_sdl_input.c
    src/bezel.c
    src/frame_server.c
    src/chat_source_json.c
    src/hal_stubs.c
    ${FW_DIR}/ui/ui_shell.c
    ${FW_DIR}/ui/ui_home.c
    ${FW_DIR}/ui/ui_messaging.c
    ${FW_DIR}/ui/ui_input.c
    ${FW_DIR}/ui/ui_leaf.c
    ${FW_DIR}/ui/ui_mainmenu.c
    ${FW_DIR}/ui/ui_sidebar.c
    ${FW_DIR}/ui/ui_keymap.c
    ${FW_DIR}/ui/ui_theme.c
    ${FW_DIR}/ui/ui_text.c
    ${FW_DIR}/ui/font_pixelify_m.c
    ${FW_DIR}/ui/ui_menu.c
    ${FW_DIR}/ui/ui_options.c
    ${FW_DIR}/ui/ui_settings.c
    ${FW_DIR}/ui/ui_speeddial.c
    ${FW_DIR}/ui/ui_heldpress.c
    ${FW_DIR}/ui/ui_calls.c
)

target_include_directories(dumbphone-desktop PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${FW_DIR}                 # ui/, hal/ headers as "ui/..." and "hal/..."
    ${SDL2_INCLUDE_DIR}
    ${JPEG_INCLUDE_DIR}
)

# Font assets are resolved at runtime; bake the source-tree path in.
target_compile_definitions(dumbphone-desktop PRIVATE
    FONT_DIR="${CMAKE_CURRENT_SOURCE_DIR}/assets/fonts"
    CHAT_DATA_PATH="${CMAKE_CURRENT_SOURCE_DIR}/data/chats.json"
    CHAT_SAMPLE_PATH="${FW_DIR}/fixtures/chats.sample.json"
)

# Optional flush instrumentation (see `just build-desktop-stats`): counts
# the bytes/areas pushed through hal_display_blit*, summary at exit.
if(FLUSH_STATS)
    target_compile_definitions(dumbphone-desktop PRIVATE UI_FLUSH_STATS=1)
endif()

target_link_libraries(dumbphone-desktop PRIVATE
    lvgl
    ${SDL2_LIBRARY}
    ${JPEG_LIBRARY}
    pthread
    m
)
