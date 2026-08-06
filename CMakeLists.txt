
add_library(light_display_ioport INTERFACE)

if(LIGHT_PLATFORM STREQUAL HOST)
        set(LIGHT_PORT host)
elseif(LIGHT_SYSTEM STREQUAL PICO_SDK AND LIGHT_PLATFORM STREQUAL TARGET)
        set(LIGHT_PORT rp2)
endif()

target_include_directories(
        light_display_ioport INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/include
)

target_sources(
        light_display_ioport INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/src/module.c
        ${CMAKE_CURRENT_LIST_DIR}/src/display_ioport.c
        ${CMAKE_CURRENT_LIST_DIR}/src/portable/${LIGHT_PORT}/platform.c
)

target_link_libraries(
        light_display_ioport INTERFACE
        light_display
        light_core
)

if(LIGHT_SYSTEM STREQUAL PICO_SDK AND LIGHT_PLATFORM STREQUAL TARGET)
        target_link_libraries(
                light_display_ioport INTERFACE
                pico_stdlib
                hardware_i2c
                hardware_spi
                hardware_dma
                hardware_pio
        )
        pico_generate_pio_header(
                light_display_ioport
                ${CMAKE_CURRENT_LIST_DIR}/src/portable/rp2/pio_spi_write.pio
                OUTPUT_DIR ${CMAKE_CURRENT_BINARY_DIR}/generated
        )
endif()
