
add_library(light_core_chip_rp2350 INTERFACE)

#   register this library as the provider of the light_object library
# for the rp2350 chip architecture
light_core_add_chip_impl(rp2350 light_core_chip_rp2350)

target_include_directories(
        light_core_chip_rp2350
        INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/include
)
target_sources(
        light_core_chip_rp2350
        INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/src/light_object.c
        ${CMAKE_CURRENT_LIST_DIR}/src/light_platform.c
)

target_link_libraries(
        light_core_chip_rp2350
        INTERFACE
        light_core
        pico_stdlib
        pico_sync
        pico_multicore
)
target_link_options(
        light_core_chip_rp2350
        INTERFACE
        "-Wl,-T,${CMAKE_CURRENT_LIST_DIR}/objects.rp2350.ld"
)
