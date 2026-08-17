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
        ${CMAKE_CURRENT_LIST_DIR}/src/clock.c
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

#   WHICH MEMORY CARRIES .data/.bss/heap. A per-project decision rather than a property of the
# chip, so it is an option rather than a constant in the linker script -- see the header of
# stm32h743xi.ld.in for what each choice costs.
#   AXI is the default because it is the safe one: it is the only choice a project that DMAs to
# a global can make, and a project that does not DMA loses only speed by taking it. DTCM is the
# opposite trade, and silently wrong the moment someone adds DMA.
set(LIGHT_H743_DATA_REGION "AXI" CACHE STRING
        "STM32H743 memory for .data/.bss/heap: AXI (DMA-reachable) or DTCM (zero wait state, no DMA)")
set_property(CACHE LIGHT_H743_DATA_REGION PROPERTY STRINGS AXI DTCM)
if(NOT LIGHT_H743_DATA_REGION STREQUAL "AXI" AND NOT LIGHT_H743_DATA_REGION STREQUAL "DTCM")
        message(FATAL_ERROR
                "LIGHT_H743_DATA_REGION must be AXI or DTCM, not '${LIGHT_H743_DATA_REGION}'")
endif()

#   the stack lives at the top of DTCM either way, so whether it shares its memory with the data
# sections depends on the choice above -- and so does the assertion that can check the fit
if(LIGHT_H743_DATA_REGION STREQUAL "DTCM")
        set(LIGHT_H743_STACK_ASSERT
                "ASSERT(_end + _Min_Stack_Size <= ORIGIN(DTCM) + LENGTH(DTCM), \"data, bss, heap and the minimum stack do not fit in DTCM together -- set LIGHT_H743_DATA_REGION=AXI\")")
else()
        set(LIGHT_H743_STACK_ASSERT
                "ASSERT(_Min_Stack_Size <= LENGTH(DTCM), \"stack does not fit in DTCM\")")
endif()

#   THE DATA CACHE, off by default because whether it is safe depends on the choice above.
#   Measured on a MiniSTM32H7xx at 400MHz, cycles of work per crossfire_task() call: 3736 with no
# caches, 2278 with the I-cache, 1888 with both. The second win is about 1.2x and it is the same
# whether .data/.bss sit in AXI or DTCM -- which is the tell that it comes from caching .rodata in
# flash at two wait states, not from caching RAM at all.
#   WHY IT IS OPTIONAL RATHER THAN ALWAYS ON: a cache the CPU owns is invisible to a DMA engine.
# With LIGHT_H743_DATA_REGION=DTCM nothing a DMA engine can reach is cached, so it is safe with no
# further work -- a DMA driver puts its buffers in AXI deliberately and handles those. With
# LIGHT_H743_DATA_REGION=AXI every global is both cached and DMA-reachable, and turning this on
# without a coherency policy (clean/invalidate around transfers, or an MPU non-cacheable window)
# buys speed in exchange for a DMA driver that reads stale data intermittently.
option(LIGHT_H743_DCACHE
        "enable the Cortex-M7 data cache (see LIGHT_H743_DATA_REGION for when this is safe)" OFF)

if(LIGHT_H743_DCACHE)
        target_compile_definitions(light_core_chip_stm32h743 INTERFACE LIGHT_H743_DCACHE=1)
        if(LIGHT_H743_DATA_REGION STREQUAL "AXI")
                message(WARNING
                        "LIGHT_H743_DCACHE is on with .data/.bss in AXI, which is DMA-reachable AND cached. "
                        "That is only correct if nothing DMAs to a global, or if this project manages cache "
                        "coherency itself. LIGHT_H743_DATA_REGION=DTCM removes the question.")
        endif()
endif()
message(STATUS "stm32h743: data cache ${LIGHT_H743_DCACHE}")

set(_light_h743_linker_script "${CMAKE_CURRENT_BINARY_DIR}/stm32h743xi.ld")
configure_file(
        "${CMAKE_CURRENT_LIST_DIR}/stm32h743xi.ld.in"
        "${_light_h743_linker_script}"
        @ONLY)
message(STATUS "stm32h743: .data/.bss/heap in ${LIGHT_H743_DATA_REGION}")

target_link_options(
        light_core_chip_stm32h743
        INTERFACE
        -mcpu=cortex-m7
        -mthumb
        -mfpu=fpv5-d16
        -mfloat-abi=hard
        "-T${_light_h743_linker_script}"
        --specs=nano.specs
        --specs=nosys.specs
        # no --gc-sections, matching the F411 port and the absence of
        # -ffunction-sections/-fdata-sections
        -Wl,-Map=$<TARGET_PROPERTY:NAME>.map,--cref
)

#   -T is just a flag as far as the generator is concerned, so without this a changed linker
# script does not relink anything: switching LIGHT_H743_DATA_REGION would reconfigure, regenerate
# the script, and then hand back the previous image unchanged. That is a bad failure to debug,
# because the build says it succeeded and the map file says the old thing.
set_property(TARGET light_core_chip_stm32h743
        APPEND PROPERTY INTERFACE_LINK_DEPENDS "${_light_h743_linker_script}")

target_link_libraries(
        light_core_chip_stm32h743
        INTERFACE
        light_core
        light_core_chip_stm32_common
)
