add_library(chip_rp2040 INTERFACE)

function(chip_rp2040_select)
        
endfunction()

light_add_chip(rp2040 arm32_m chip_rp2040_select)

                  
target_include_directories(
    chip_rp2040
    INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/include
)
 
target_sources(
    chip_rp2040
    INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/src/chip.c
)

target_link_libraries(
    chip_rp2040
    INTERFACE

)
