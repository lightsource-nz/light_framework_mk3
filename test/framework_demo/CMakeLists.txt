
add_executable(framework_demo)

target_sources(framework_demo PUBLIC ${CMAKE_CURRENT_LIST_DIR}/src/framework_demo.c)

target_link_libraries(
    framework_demo PUBLIC
    light_core
    light_cli
)

if(LIGHT_SYSTEM STREQUAL PICO_SDK AND NOT PICO_NO_HARDWARE)
        pico_enable_stdio_semihosting(framework_demo 1)
        target_link_libraries(framework_demo PUBLIC pico_stdlib)
endif()
