#   light_core implementation for the STM32F411, built on bare CMSIS: ST's register headers,
# startup and system_stm32f4xx.c, and nothing above them. No HAL, no LL.
#
#   Unlike the RP2 ports there is no vendor SDK CMake package to hand this work to -- pico-sdk
# supplies compiler flags, a linker script and a startup path of its own, whereas
# cmsis_device_f4 is a bundle of files. So the flags, the linker script and the startup object
# are all named here.

add_library(light_core_chip_stm32f411 INTERFACE)

# register this library as the provider of light_core for the stm32f411 chip
light_core_add_chip_impl(stm32f411 light_core_chip_stm32f411)

set(_cmsis_core ${LIGHT_PATH}/lib/CMSIS_6/CMSIS/Core/Include)
set(_cmsis_f4 ${LIGHT_PATH}/lib/cmsis_device_f4)

if(NOT EXISTS ${_cmsis_core}/core_cm4.h OR NOT EXISTS ${_cmsis_f4}/Include/stm32f411xe.h)
        message(FATAL_ERROR
                "CMSIS sources are missing under ${LIGHT_PATH}/lib -- this port vendors them as "
                "submodules, so a clone without --recursive lands here. "
                "run: git -C ${LIGHT_PATH} submodule update --init --recursive")
endif()

target_include_directories(
        light_core_chip_stm32f411
        INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/include
        ${_cmsis_core}
        ${_cmsis_f4}/Include
)

target_sources(
        light_core_chip_stm32f411
        INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/src/light_object.c
        ${CMAKE_CURRENT_LIST_DIR}/src/light_platform.c
        ${CMAKE_CURRENT_LIST_DIR}/src/console.c
        # ST's, not ours. system_stm32f4xx.c maintains SystemCoreClock, which
        # light_platform_init() derives the SysTick reload from; the startup file provides the
        # vector table, Reset_Handler, and the weak default handlers that SysTick_Handler in
        # light_platform.c overrides
        ${_cmsis_f4}/Source/Templates/system_stm32f4xx.c
        ${_cmsis_f4}/Source/Templates/gcc/startup_stm32f411xe.s
)

target_compile_definitions(
        light_core_chip_stm32f411
        INTERFACE
        # selects the part within the family. stm32f4xx.h #errors out if no device is defined,
        # so this is required rather than merely advisable
        STM32F411xE
)

target_compile_options(
        light_core_chip_stm32f411
        INTERFACE
        # Cortex-M4 with the single-precision FPU. -mfloat-abi=hard rather than softfp: nothing
        # here links a precompiled softfp library, and mixing the two is a link error rather
        # than a silent slowdown
        -mcpu=cortex-m4
        -mthumb
        -mfpu=fpv4-sp-d16
        -mfloat-abi=hard
        # the framework's static-registration sections are reachable only through a linker
        # walk, so nothing references them and --gc-sections would collect them. the KEEP()s in
        # the linker script are what protect those; -ffunction-sections/-fdata-sections are
        # kept off entirely rather than relying on KEEP alone, since this port has no size
        # pressure worth that risk yet
)

target_link_options(
        light_core_chip_stm32f411
        INTERFACE
        -mcpu=cortex-m4
        -mthumb
        -mfpu=fpv4-sp-d16
        -mfloat-abi=hard
        # the whole memory layout, not an INSERT fragment -- see the header of the script
        "-T${CMAKE_CURRENT_LIST_DIR}/stm32f411ce.ld"
        # newlib-nano, and no host OS underneath it. --specs=nosys.specs supplies the stub
        # _write/_sbrk/_close syscalls newlib wants at link time; without it a printf anywhere
        # in the tree fails to link with a wall of undefined references that name none of this
        --specs=nano.specs
        --specs=nosys.specs
        # no --gc-sections, to match the absence of -ffunction-sections/-fdata-sections above.
        # Turning it on without those buys almost nothing and puts the framework's
        # linker-walked sections behind the KEEP()s alone; worth revisiting together, under
        # actual size pressure, rather than one without the other
        -Wl,-Map=$<TARGET_PROPERTY:NAME>.map,--cref
)

target_link_libraries(
        light_core_chip_stm32f411
        INTERFACE
        light_core
)
