
add_library(light_core_arch_arm32_m INTERFACE)

# register this library as the provider of the light_object library for the arm32_m architecture
light_core_add_arch_impl(arm32_m light_core_arch_arm32_m)

target_include_directories(
        light_core_arch_arm32_m
        INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/include
)
target_sources(
        light_core_arch_arm32_m
        INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/src/light_core_arm32_m.c
)

target_link_libraries(
        light_core_arch_arm32_m
        INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/objects.arm32_m.ld
)
