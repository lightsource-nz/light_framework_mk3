
add_library(light_core_arch_host_os INTERFACE)

# register this library as the provider of the light_object library for the host_os architecture
light_core_add_arch_impl(host_os light_core_arch_host_os)

target_include_directories(
        light_core_arch_host_os
        INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/include
)
target_sources(
        light_core_arch_host_os
        INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/src/light_object.c
        ${CMAKE_CURRENT_LIST_DIR}/src/light_platform.c
)

target_link_libraries(
        light_core_arch_host_os
        INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/objects.host_os.ld
        rt
        light_core
)
