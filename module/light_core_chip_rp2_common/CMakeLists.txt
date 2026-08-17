
# code shared by the RP2040 and RP2350 chip ports. Both present the same Pico SDK peripheral
# API, so anything that would otherwise be copied verbatim between them belongs here rather
# than in either -- and not in light_core_arch_arm32_m, which is shared with any future ARM
# chip that would inherit RP2 peripheral code it cannot run.
add_library(light_core_chip_rp2_common INTERFACE)

target_sources(
        light_core_chip_rp2_common
        INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/src/light_platform_pwm.c
        ${CMAKE_CURRENT_LIST_DIR}/src/light_platform_gpio.c
)

target_link_libraries(
        light_core_chip_rp2_common
        INTERFACE
        light_core
        hardware_clocks
        hardware_dma
        hardware_gpio
        hardware_pwm
)
