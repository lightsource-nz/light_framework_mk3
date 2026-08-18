#   light_core implementation for the STM32F103 (BluePill/BluePill+), built on bare CMSIS: ST's
# register headers, startup and system_stm32f1xx.c, and nothing above them. See the F411 port's
# CMakeLists.txt for why there is no vendor SDK package to hand this to.

add_library(light_core_chip_stm32f103 INTERFACE)

# register this library as the provider of light_core for the stm32f103 chip
light_core_add_chip_impl(stm32f103 light_core_chip_stm32f103)

set(_cmsis_core ${LIGHT_PATH}/lib/CMSIS_6/CMSIS/Core/Include)
set(_cmsis_f1 ${LIGHT_PATH}/lib/cmsis_device_f1)

if(NOT EXISTS ${_cmsis_core}/core_cm3.h OR NOT EXISTS ${_cmsis_f1}/Include/stm32f103xb.h)
        message(FATAL_ERROR
                "CMSIS sources are missing under ${LIGHT_PATH}/lib -- this port vendors them as "
                "submodules, so a clone without --recursive lands here. "
                "run: git -C ${LIGHT_PATH} submodule update --init --recursive")
endif()

target_include_directories(
        light_core_chip_stm32f103
        INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/include
        ${_cmsis_core}
        ${_cmsis_f1}/Include
)

target_sources(
        light_core_chip_stm32f103
        INTERFACE
        # light_object.c and light_platform.c live in light_core_chip_stm32_common: they are
        # CMSIS-core only and identical on every STM32. light_platform_gpio.c and
        # light_platform_pwm.c also live there, with STM32F103xB branches for the parts where
        # this chip's GPIO block genuinely differs (CRL/CRH, no MODER/AFR). console.c stays
        # here because the USART pin setup is the family-specific part of it.
        ${CMAKE_CURRENT_LIST_DIR}/src/console.c
        ${_cmsis_f1}/Source/Templates/system_stm32f1xx.c
        ${_cmsis_f1}/Source/Templates/gcc/startup_stm32f103xb.s
)

target_compile_definitions(
        light_core_chip_stm32f103
        INTERFACE
        # selects the part within the family, as STM32F411xE does for the F4 port.
        # STM32F103xB covers both the 64K (C8T6, this board) and 128K (CBT6) parts -- the
        # peripheral set is identical, only flash/RAM size differs, which the linker script
        # below (not this macro) is what actually needs to get right
        STM32F103xB
)

target_compile_options(
        light_core_chip_stm32f103
        INTERFACE
        # Cortex-M3, no FPU -- unlike the F411's M4F, so no -mfpu/-mfloat-abi here. Getting
        # -mfloat-abi wrong on a core with no FPU is not a slow path, it is a build that will not
        # run at all
        -mcpu=cortex-m3
        -mthumb
        # as on the F411 port: no -ffunction-sections/-fdata-sections, so no --gc-sections, so
        # the KEEP()s in the linker script are what protect the framework's static-registration
        # sections rather than an assumption those flags would otherwise need
)

target_link_options(
        light_core_chip_stm32f103
        INTERFACE
        -mcpu=cortex-m3
        -mthumb
        "-T${CMAKE_CURRENT_LIST_DIR}/stm32f103c8.ld"
        --specs=nano.specs
        --specs=nosys.specs
        -Wl,-Map=$<TARGET_PROPERTY:NAME>.map,--cref
)

target_link_libraries(
        light_core_chip_stm32f103
        INTERFACE
        light_core
        light_core_chip_stm32_common
)
