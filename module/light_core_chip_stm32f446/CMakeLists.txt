#   light_core implementation for the STM32F446, built on bare CMSIS: ST's register headers,
# startup and system_stm32f4xx.c, and nothing above them -- see the F411 port's CMakeLists.txt
# for why there is no vendor SDK package to hand this to.

add_library(light_core_chip_stm32f446 INTERFACE)

# register this library as the provider of light_core for the stm32f446 chip
light_core_add_chip_impl(stm32f446 light_core_chip_stm32f446)

set(_cmsis_core ${LIGHT_PATH}/lib/CMSIS_6/CMSIS/Core/Include)
set(_cmsis_f4 ${LIGHT_PATH}/lib/cmsis_device_f4)

if(NOT EXISTS ${_cmsis_core}/core_cm4.h OR NOT EXISTS ${_cmsis_f4}/Include/stm32f446xx.h)
        message(FATAL_ERROR
                "CMSIS sources are missing under ${LIGHT_PATH}/lib -- this port vendors them as "
                "submodules, so a clone without --recursive lands here. "
                "run: git -C ${LIGHT_PATH} submodule update --init --recursive")
endif()

target_include_directories(
        light_core_chip_stm32f446
        INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/include
        ${_cmsis_core}
        ${_cmsis_f4}/Include
)

target_sources(
        light_core_chip_stm32f446
        INTERFACE
        # light_object.c, light_platform.c, light_platform_gpio.c and light_platform_pwm.c live
        # in light_core_chip_stm32_common: this chip's GPIO/PWM register layout is the same as
        # the F411's (both fall into that module's generic "else" branch), so nothing there
        # needed a new STM32F446xx-specific case. console.c stays here, matching every other
        # chip port, since the console is the one piece a board could plausibly move
        ${CMAKE_CURRENT_LIST_DIR}/src/console.c
        ${_cmsis_f4}/Source/Templates/system_stm32f4xx.c
        ${_cmsis_f4}/Source/Templates/gcc/startup_stm32f446xx.s
)

target_compile_definitions(
        light_core_chip_stm32f446
        INTERFACE
        # a single macro for the whole F446 family regardless of flash density (unlike F411,
        # which has separate x8/xC/xE macros) -- ST's own stm32f4xx.h dispatches on this alone
        STM32F446xx
)

target_compile_options(
        light_core_chip_stm32f446
        INTERFACE
        # Cortex-M4 with the single-precision FPU, same core as the F411 port
        -mcpu=cortex-m4
        -mthumb
        -mfpu=fpv4-sp-d16
        -mfloat-abi=hard
)

target_link_options(
        light_core_chip_stm32f446
        INTERFACE
        -mcpu=cortex-m4
        -mthumb
        -mfpu=fpv4-sp-d16
        -mfloat-abi=hard
        "-T${CMAKE_CURRENT_LIST_DIR}/stm32f446xe.ld"
        --specs=nano.specs
        --specs=nosys.specs
        -Wl,-Map=$<TARGET_PROPERTY:NAME>.map,--cref
)

target_link_libraries(
        light_core_chip_stm32f446
        INTERFACE
        light_core
        light_core_chip_stm32_common
)
