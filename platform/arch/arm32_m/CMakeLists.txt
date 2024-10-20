add_library(arch_arm32_m INTERFACE)

function(arch_arm32_m_select)
        
endfunction()

# arch arm32_m: bits=32, mmu=0
light_add_arch(arm32_m 32 0 arch_arm32_m_select)
                  
target_include_directories(
        arch_arm32_m
        INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/include
)
 
target_sources(
        arch_arm32_m
        INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/src/arch.c
)

