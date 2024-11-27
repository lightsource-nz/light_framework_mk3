# board_pico_hostmode: support for pico SDK host-based builds
add_library(board_pico_hostmode INTERFACE)
function(board_pico_hostmode_select)

endfunction()

light_add_board(pico_hostmode pico_hostmode board_pico_hostmode_select)
                  
target_include_directories(
    board_pico_hostmode
    INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/include
)
 
target_sources(
    board_pico_hostmode
    INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/src/board.c
)
