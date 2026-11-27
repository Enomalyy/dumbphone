# ui/CMakeLists.txt — one boundary, two build systems.
#
# The same file is a static library for the desktop/web build and an ESP-IDF
# component for the device image. The source list is the glob in
# ../sources.cmake (driven by ../libs.txt), so nothing here names files.
#
# Include dirs carry both the boundary itself ("ui_menu.h") and the firmware
# root ("hal/hal_display.h", "ui/ui_port.h") — the two styles the UI uses.
include(${CMAKE_CURRENT_LIST_DIR}/../sources.cmake)

if(ESP_PLATFORM)
    dp_boundary(ui REQUIRES lvgl hal model)
else()
    dp_boundary(ui lvgl dp_hal dp_model)
endif()
