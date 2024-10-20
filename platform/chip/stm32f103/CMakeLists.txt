add_library(chip_stm32f103 INTERFACE)

function(chip_stm32f103_select)
        
endfunction()

# arch arm32_m: bits=32, mmu=0
light_add_chip(stm32f103 arm32_m chip_stm32f103_select)

                  
target_include_directories(
    chip_stm32f103
    INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/include
)
 
target_sources(
    chip_stm32f103
    INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/src/chip.c
)

target_link_libraries(
    chip_stm32f103
    INTERFACE

)
