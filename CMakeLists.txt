
add_library(light_ioport INTERFACE)

if(LIGHT_PLATFORM STREQUAL HOST)
        set(LIGHT_PORT host)
elseif(LIGHT_SYSTEM STREQUAL PICO_SDK AND LIGHT_PLATFORM STREQUAL TARGET)
        set(LIGHT_PORT rp2)
elseif(LIGHT_SYSTEM STREQUAL CMSIS AND LIGHT_PLATFORM STREQUAL TARGET)
        set(LIGHT_PORT stm32)
else()
        # an unset LIGHT_PORT used to fall through to src/portable/platform.c, and the error
        # was CMake reporting a missing source file with an empty directory in the middle of
        # its path -- which says nothing about the actual problem
        message(FATAL_ERROR
                "light_ioport has no platform implementation for LIGHT_SYSTEM=${LIGHT_SYSTEM} "
                "with LIGHT_PLATFORM=${LIGHT_PLATFORM}. Supported: HOST (any system), "
                "PICO_SDK/TARGET, CMSIS/TARGET.")
endif()

target_include_directories(
        light_ioport INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/include
)

target_sources(
        light_ioport INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/src/module.c
        ${CMAKE_CURRENT_LIST_DIR}/src/display_ioport.c
        ${CMAKE_CURRENT_LIST_DIR}/src/portable/${LIGHT_PORT}/platform.c
)

#   NO light_display here, and its absence is the point. This module named light_display for
# as long as it has existed, which made the two mutually dependent -- light_display links
# light_ioport for its transports, and light_ioport linked light_display back. CMake tolerates
# that between static and INTERFACE libraries by repeating them on the link line, so it never
# failed and nobody noticed.
#   it was never real: nothing under src/ or include/ references a light_display type, function
# or header. The only mentions in the whole module are this line and one comment in the STM32
# port. Dropping it costs nothing and leaves light_ioport genuinely standalone -- a transport
# abstraction over I2C/SPI that light_display, light_touch and light_imu all sit on top of,
# rather than a peer of the display layer. That is what lets it live in the framework.
target_link_libraries(
        light_ioport INTERFACE
        light_core
)

if(LIGHT_SYSTEM STREQUAL PICO_SDK AND LIGHT_PLATFORM STREQUAL TARGET)
        target_link_libraries(
                light_ioport INTERFACE
                pico_stdlib
                hardware_i2c
                hardware_spi
                hardware_dma
                hardware_pio
        )
        pico_generate_pio_header(
                light_ioport
                ${CMAKE_CURRENT_LIST_DIR}/src/portable/rp2/pio_spi_write.pio
                OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated
        )
endif()
