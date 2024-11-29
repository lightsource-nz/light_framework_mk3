
add_executable(demo_cli)

target_sources(demo_cli PUBLIC ${CMAKE_CURRENT_LIST_DIR}/src/demo_cli.c)

target_link_libraries(
    demo_cli PUBLIC
    light_core
    light_cli
)

if(LIGHT_SYSTEM STREQUAL PICO_SDK AND NOT PICO_NO_HARDWARE)
        pico_enable_stdio_semihosting(demo_cli 1)
        target_link_libraries(demo_cli PUBLIC pico_stdlib)
endif()
