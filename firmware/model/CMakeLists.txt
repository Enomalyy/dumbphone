# model/CMakeLists.txt — the data layer, same boundary for both builds.
# Plain CMake static library; ESP-IDF component. Sources come from
# ../sources.cmake (../libs.txt); no file is named here.
include(${CMAKE_CURRENT_LIST_DIR}/../sources.cmake)

if(ESP_PLATFORM)
    dp_boundary(model REQUIRES hal)
else()
    dp_boundary(model dp_hal)
endif()
