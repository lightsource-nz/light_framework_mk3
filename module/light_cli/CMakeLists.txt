# module/light_cli
# 
# defines the light_cli library, which provides functionality
# for command-line applications built on the light framework

add_library(light_cli INTERFACE)

target_include_directories(
        light_cli INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/include
)

target_sources(
        light_cli INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/src/module.c
        ${CMAKE_CURRENT_LIST_DIR}/src/command.c
        ${CMAKE_CURRENT_LIST_DIR}/src/message.c
)

target_link_libraries(
        light_cli INTERFACE
        light_core
)