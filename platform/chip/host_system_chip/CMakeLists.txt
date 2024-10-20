add_library(chip_host_system INTERFACE)

function(chip_host_system_select)
        
endfunction()

# arch arm32_m: bits=32, mmu=0
light_add_chip(host_system host_system chip_host_system_select)

                  
target_include_directories(
    chip_host_system
    INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/include
)
 
target_sources(
    chip_host_system
    INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/src/chip.c
)

target_link_libraries(
    chip_host_system
    INTERFACE

)
