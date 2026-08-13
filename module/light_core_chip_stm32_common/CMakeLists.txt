#   the parts of an STM32 light_core port that are the same on every STM32, shared by the chip
# ports rather than copied into each. Mirrors light_core_chip_rp2_common, and for the same
# reason: two copies of a file diverge, and the divergence is only noticed when one of them is
# wrong.
#
#   What is here is CMSIS-core only -- SysTick, SystemCoreClock, __WFI, PRIMASK -- and names no
# peripheral and no chip. It reaches the right vendor header (stm32f4xx.h, stm32h7xx.h, ...)
# through light.h -> light_core_port.h, which each chip port supplies, so these sources compile
# unchanged against any of them.
#
#   What stays per-chip: light_core_port.h and light_platform_port.h (types, capabilities and
# the vendor include), console.c (the USART register interface differs between families --
# F4 uses SR/DR where H7 uses ISR/TDR), the linker script, and the architecture flags.

add_library(light_core_chip_stm32_common INTERFACE)

target_sources(
        light_core_chip_stm32_common
        INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/src/light_object.c
        ${CMAKE_CURRENT_LIST_DIR}/src/light_platform.c
        ${CMAKE_CURRENT_LIST_DIR}/src/light_platform_pwm.c
)

target_link_libraries(
        light_core_chip_stm32_common
        INTERFACE
        light_core
)
