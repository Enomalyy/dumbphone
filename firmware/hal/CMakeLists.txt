# hal/CMakeLists.txt — the HAL contract, and the device side of it.
#
# On the desktop build hal is headers only: the simulator's backends live in
# host/src/ (SDL2), so this boundary contributes includes, not objects.
# On the device build the backends exist — hal/esp32s3/ is the same boundary,
# and the IDF build compiles it: the display flush, the tick, the NVS settings
# store, the GPIO keypad, and ui_port_esp32.c (the chip's ui_port.h).
#
# The TTF faces are embedded in the image (decision 27/11, decisions.md): the
# device has no filesystem, so ui_port_font() gets a flash pointer. The names
# come from the directory, never a per-build list.
include(${CMAKE_CURRENT_LIST_DIR}/../sources.cmake)

if(ESP_PLATFORM)
    file(GLOB DP_HAL_SOURCES CONFIGURE_DEPENDS ${CMAKE_CURRENT_LIST_DIR}/esp32s3/*.c)
    file(GLOB DP_FONT_FILES CONFIGURE_DEPENDS ${DP_ROOT}/host/assets/fonts/*.ttf)

    idf_component_register(SRCS ${DP_HAL_SOURCES}
                           INCLUDE_DIRS "${CMAKE_CURRENT_LIST_DIR}" ${DP_ROOT}
                           EMBED_FILES ${DP_FONT_FILES}
                           REQUIRES lvgl nvs_flash esp_lcd esp_timer driver esp_driver_gpio)
else()
    dp_boundary(hal lvgl)
endif()
