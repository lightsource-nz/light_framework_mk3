add_library(light_chip_stm32h743 INTERFACE)

function(chip_stm32h743_select)
        
endfunction()

# arch arm32_m: bits=32, mmu=0
light_add_chip(stm32h743 arm32_m chip_stm32h743_select)

                  
target_include_directories(
    light_chip_stm32h743
    INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/include
)
 
target_sources(
    light_chip_stm32h743
    INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/src/chip.c
)

target_link_libraries(
    light_chip_stm32h743
    INTERFACE

)
