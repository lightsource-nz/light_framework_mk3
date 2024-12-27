
add_library(light_core_board_pico_hostmode INTERFACE)

#   register this library as the provider of the light_object library
# for the rp2040 chip architecture
light_core_add_board_impl(pico_hostmode light_core_board_pico_hostmode)

target_include_directories(
        light_core_board_pico_hostmode
        INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/include
)
target_sources(
        light_core_board_pico_hostmode
        INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/src/light_object.c
        ${CMAKE_CURRENT_LIST_DIR}/src/light_platform.c
)

target_link_libraries(
        light_core_board_pico_hostmode
        INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/objects.pico_hostmode.ld
        light_core
        pico_stdlib
        pico_sync
)
