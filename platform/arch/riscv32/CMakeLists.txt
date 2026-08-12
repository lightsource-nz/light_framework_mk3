add_library(arch_riscv32 INTERFACE)

function(arch_riscv32_select)

endfunction()

# arch riscv32: bits=32, mmu=0
light_add_arch(riscv32 32 0 arch_riscv32_select)

target_include_directories(
        arch_riscv32
        INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/include
)

target_sources(
        arch_riscv32
        INTERFACE
        ${CMAKE_CURRENT_LIST_DIR}/src/arch.c
)
