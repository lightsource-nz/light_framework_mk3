add_library(chip_pico_hostmode INTERFACE)

function(chip_pico_hostmode_select)
        
endfunction()

light_add_chip(pico_hostmode host_os chip_pico_hostmode_select)

                  
target_include_directories(
    chip_pico_hostmode
    INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/include
)
 
target_sources(
    chip_pico_hostmode
    INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/src/chip.c
)

target_link_libraries(
    chip_pico_hostmode
    INTERFACE
    
)
