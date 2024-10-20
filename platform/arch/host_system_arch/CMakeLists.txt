add_library(arch_host_system INTERFACE)

function(arch_host_system_select)
        
endfunction()

# arch host_system: bits=32, mmu=1
light_add_arch(host_system 32 1 arch_host_system_select)
                  
target_include_directories(
        arch_host_system
        INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/include
)
 
target_sources(
        arch_host_system
        INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/src/arch.c
)

