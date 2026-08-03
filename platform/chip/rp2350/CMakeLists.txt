add_library(chip_rp2350 INTERFACE)

function(chip_rp2350_select)

endfunction()

light_add_chip(rp2350 arm32_m chip_rp2350_select)


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
