add_library(chip_rp2350 INTERFACE)

function(chip_rp2350_select)

endfunction()

light_add_chip(rp2350 arm32_m chip_rp2350_select)
# RP2350 carries two CPU architectures on the die: a pair of Cortex-M33 cores and a pair of
# Hazard3 RISC-V cores, of which any two can be running. which pair an image targets is a
# build-time choice -- select it with -DLIGHT_ARCH=riscv32. arm32_m stays the default above,
# so a build that says nothing about arch is unaffected
light_add_chip_arch(rp2350 riscv32)


target_include_directories(
    chip_rp2350
    INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/include
)

target_sources(
    chip_rp2350
    INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/src/chip.c
)

target_link_libraries(
    chip_rp2350
    INTERFACE

)
