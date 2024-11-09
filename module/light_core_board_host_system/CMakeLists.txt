
add_library(light_core_board_host_system INTERFACE)

# register this library as the provider of the light_object library for the host_system architecture
light_core_add_board_impl(host_system light_core_board_host_system)

target_include_directories(
        light_core_board_host_system
        INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/include
)
target_sources(
        light_core_board_host_system
        INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/src/light_object.c
        ${CMAKE_CURRENT_LIST_DIR}/src/light_platform.c
)

target_link_libraries(
        light_core_board_host_system
        INTERFACE
        rt
        light_core
        ${LIGHT_CORE_LINK_DIR}/objects.host_system.ld
)
