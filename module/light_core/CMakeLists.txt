# module/light_core
# 
# root module of the 'light' embedded application framework.
# defines the light_core library, which should be linked
# into any module or application which uses the framework

add_library(light_core INTERFACE)

include(light_core.cmake)

target_include_directories(
        light_core INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/include
)

target_sources(
        light_core INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/src/framework.c
        ${CMAKE_CURRENT_LIST_DIR}/src/light_common.c
        ${CMAKE_CURRENT_LIST_DIR}/src/list.c
)

if(LIGHT_SYSTEM STREQUAL PICO_SDK)
        target_link_libraries(
                light_core
                INTERFACE
                pico_platform
                pico_sync
        )
endif()
