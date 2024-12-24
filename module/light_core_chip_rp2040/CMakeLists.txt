
add_library(light_core_chip_rp2040 INTERFACE)

#   register this library as the provider of the light_object library
# for the rp2040 chip architecture
light_core_add_chip_impl(rp2040 light_core_chip_rp2040)

target_include_directories(
        light_core_chip_rp2040
        INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/include
)
target_sources(
        light_core_chip_rp2040
        INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/src/light_object.c
        ${CMAKE_CURRENT_LIST_DIR}/src/light_platform.c
)

target_link_libraries(
        light_core_chip_rp2040
        INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/objects.rp2040.ld
        light_core
        pico_stdlib
        pico_sync
)
