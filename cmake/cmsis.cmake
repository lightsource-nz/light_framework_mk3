# cmsis.cmake
#   defines light framework integration for bare-CMSIS targets (LIGHT_SYSTEM=CMSIS).
#
#   Far smaller than pico_sdk.cmake, and that asymmetry is the point of choosing bare CMSIS:
# there is no SDK to initialise, no package to import and no build system to hand control to.
# The vendored device pack is a bundle of headers, one startup file and one system_*.c, all of
# which the chip module names directly. What is left for this file is the parts of a build a
# vendor SDK would otherwise own -- the toolchain, and turning ELFs into something flashable.

macro(light_cmsis_select_toolchain)
        # before project(), or CMake has already probed the host compiler by the time this runs
        if(NOT CMAKE_TOOLCHAIN_FILE)
                set(CMAKE_TOOLCHAIN_FILE "${LIGHT_PATH}/cmake/toolchain/arm_none_eabi.cmake"
                        CACHE FILEPATH "cross toolchain for bare-metal ARM targets")
        endif()
endmacro()

# light_cmsis_add_extra_outputs(<target>)
#   the equivalent of pico_add_extra_outputs(): produces the .bin and .hex a flashing tool
# actually consumes, and prints the size breakdown. Called through the target-property hook
# below, so an application marked as such gets them without naming this itself.
function(light_cmsis_add_extra_outputs TARGET)
        add_custom_command(TARGET ${TARGET} POST_BUILD
                COMMAND ${CMAKE_OBJCOPY} -O binary $<TARGET_FILE:${TARGET}> $<TARGET_FILE_DIR:${TARGET}>/${TARGET}.bin
                COMMAND ${CMAKE_OBJCOPY} -O ihex $<TARGET_FILE:${TARGET}> $<TARGET_FILE_DIR:${TARGET}>/${TARGET}.hex
                COMMAND ${CMAKE_SIZE} $<TARGET_FILE:${TARGET}>
                COMMENT "generating .bin/.hex for ${TARGET}"
                VERBATIM)
endfunction()

macro(light_cmsis_target_property_hook TARGET PROPERTY VALUE)
        if(${PROPERTY} STREQUAL "application_target" AND ${VALUE} EQUAL 1)
                light_cmsis_add_extra_outputs(${TARGET})
        endif()
endmacro()

function(light_load_cmsis_support)
        message("Loading bare-CMSIS support")
        light_hook_set_target_property_global(light_cmsis_target_property_hook)
endfunction()
