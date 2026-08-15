# module/light_core
# 
# root module of the 'light' embedded application framework.
# defines the light_core library, which should be linked
# into any module or application which uses the framework

add_library(light_core INTERFACE)

include(light_core.cmake)

#   generates light_version.h from git, regenerated on every build so a binary cannot report a
# commit it was not built from. Included here rather than at the top level because light.h is
# what consumes it, and light_core is what owns light.h
include(light_version)

target_include_directories(
        light_core INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/include
        ${LIGHT_VERSION_INCLUDE_DIR}
)

# so anything linking light_core waits for the header to be refreshed before compiling
add_dependencies(light_core light_version_header)

target_sources(
        light_core INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/src/framework.c
        ${CMAKE_CURRENT_LIST_DIR}/src/light_common.c
        ${CMAKE_CURRENT_LIST_DIR}/src/list.c
        ${CMAKE_CURRENT_LIST_DIR}/src/stream.c
)

if(LIGHT_SYSTEM STREQUAL PICO_SDK)
        target_link_libraries(
                light_core
                INTERFACE
                pico_stdlib
        )
endif()
