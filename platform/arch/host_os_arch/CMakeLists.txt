add_library(arch_host_os INTERFACE)

function(arch_host_os_select)
        
endfunction()

# arch host_os: bits=32, mmu=1
light_add_arch(host_os 32 1 arch_host_os_select)
                  
target_include_directories(
        arch_host_os
        INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/include
)
 
target_sources(
        arch_host_os
        INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/src/arch.c
)

