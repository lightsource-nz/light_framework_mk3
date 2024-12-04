add_library(chip_host_os INTERFACE)

function(chip_host_os_select)
        
endfunction()

# arch arm32_m: bits=32, mmu=0
light_add_chip(host_os host_os chip_host_os_select)

                  
target_include_directories(
    chip_host_os
    INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/include
)
 
target_sources(
    chip_host_os
    INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/src/chip.c
)

target_link_libraries(
    chip_host_os
    INTERFACE

)
