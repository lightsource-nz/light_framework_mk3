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
)

target_link_libraries(
        light_cli INTERFACE
        light_core
)

#   LIGHT_BOOT_COMMAND bakes a command line into the image, for targets that have no console to
# type one at. light_cli runs it once at application launch, exactly as though it had arrived on
# argv, so a board's setup can be expressed as a command and changed from a preset without
# touching application code.
#
#   empty by default: an application that passes a real argv (any host program) is unaffected,
# and a target app that passes (0, NULL) simply has nothing to run, as before
set(LIGHT_BOOT_COMMAND "" CACHE STRING
        "command line baked into the image and run once at application launch, e.g. \"display init --rotation 90\"")

if(NOT LIGHT_BOOT_COMMAND STREQUAL "")
        light_info("baking boot command: '${LIGHT_BOOT_COMMAND}'")
        target_compile_definitions(
                light_cli INTERFACE
                LIGHT_BOOT_COMMAND="${LIGHT_BOOT_COMMAND}"
        )
endif()