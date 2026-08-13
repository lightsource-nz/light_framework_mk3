#   light_core implementation for the STM32H743, on bare CMSIS. Structurally the same as the
# F411 port: light_object.c and light_platform.c come from light_core_chip_stm32_common, and
# what is here is the part that genuinely differs -- the port headers, the console (a different
# USART generation), the memory map, and the architecture flags.

add_library(light_core_chip_stm32h743 INTERFACE)

light_core_add_chip_impl(stm32h743 light_core_chip_stm32h743)

set(_cmsis_core ${LIGHT_PATH}/lib/CMSIS_6/CMSIS/Core/Include)
set(_cmsis_h7 ${LIGHT_PATH}/lib/cmsis_device_h7)

if(NOT EXISTS ${_cmsis_core}/core_cm7.h OR NOT EXISTS ${_cmsis_h7}/Include/stm32h743xx.h)
        message(FATAL_ERROR
                "CMSIS sources are missing under ${LIGHT_PATH}/lib -- this port vendors them as "
                "submodules, so a clone without --recursive lands here. "
                "run: git -C ${LIGHT_PATH} submodule update --init --recursive")
endif()

target_include_directories(
        light_core_chip_stm32h743
        INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/include
        ${_cmsis_core}
        ${_cmsis_h7}/Include
)

target_sources(
        light_core_chip_stm32h743
        INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/src/console.c
        ${_cmsis_h7}/Source/Templates/system_stm32h7xx.c
        ${_cmsis_h7}/Source/Templates/gcc/startup_stm32h743xx.s
)

target_compile_definitions(
        light_core_chip_stm32h743
        INTERFACE
        # selects the part within the family; stm32h7xx.h #errors out without one
        STM32H743xx
)

target_compile_options(
        light_core_chip_stm32h743
        INTERFACE
        # Cortex-M7 with the double-precision FPU. -mfpu=fpv5-d16 rather than fpv5-sp-d16: this
        # part has the full DP unit, and picking the SP variant would silently send every
        # double through software emulation
        -mcpu=cortex-m7
        -mthumb
        -mfpu=fpv5-d16
        -mfloat-abi=hard
)

target_link_options(
        light_core_chip_stm32h743
        INTERFACE
        -mcpu=cortex-m7
        -mthumb
        -mfpu=fpv5-d16
        -mfloat-abi=hard
        "-T${CMAKE_CURRENT_LIST_DIR}/stm32h743xi.ld"
        --specs=nano.specs
        --specs=nosys.specs
        # no --gc-sections, matching the F411 port and the absence of
        # -ffunction-sections/-fdata-sections
        -Wl,-Map=$<TARGET_PROPERTY:NAME>.map,--cref
)

target_link_libraries(
        light_core_chip_stm32h743
        INTERFACE
        light_core
        light_core_chip_stm32_common
)
