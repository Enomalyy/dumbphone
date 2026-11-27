# sources.cmake — reads libs.txt and turns each boundary into a target.
#
# Included by the plain-CMake desktop build (host/CMakeLists.txt) and by each
# boundary's own CMakeLists.txt, which is dual-mode: the same file is a static
# library for the desktop and an ESP-IDF component for the device. Globbing is
# deliberate — a boundary is a directory and every file in it belongs to it.
#
# ARGN is the dependency list: idf_component_register REQUIRES for IDF,
# target_link_libraries for plain CMake.

get_filename_component(DP_ROOT ${CMAKE_CURRENT_LIST_DIR} ABSOLUTE)

function(dp_boundary_sources boundary out_var)
    file(GLOB found CONFIGURE_DEPENDS ${DP_ROOT}/${boundary}/*.c)
    set(${out_var} "${found}" PARENT_SCOPE)
endfunction()

function(dp_boundary boundary)
    dp_boundary_sources(${boundary} srcs)

    if(ESP_PLATFORM)
        if(NOT srcs)
            # A source-less boundary is the contract itself: an IDF component
            # carrying only its headers (see libs.txt for hal/).
            idf_component_register(INCLUDE_DIRS "${DP_ROOT}/${boundary}" ${DP_ROOT} ${ARGN})
            return()
        endif()
        idf_component_register(SRCS ${srcs}
                               INCLUDE_DIRS "${DP_ROOT}/${boundary}" ${DP_ROOT}
                               ${ARGN})
        return()
    endif()

    if(srcs)
        add_library(dp_${boundary} STATIC ${srcs})
        target_include_directories(dp_${boundary} PUBLIC ${DP_ROOT}/${boundary} ${DP_ROOT})
        target_link_libraries(dp_${boundary} PUBLIC ${ARGN})
    else()
        # Headers only: the target supplies the backends.
        add_library(dp_${boundary} INTERFACE)
        target_include_directories(dp_${boundary} INTERFACE ${DP_ROOT}/${boundary} ${DP_ROOT})
        target_link_libraries(dp_${boundary} INTERFACE ${ARGN})
    endif()
endfunction()
