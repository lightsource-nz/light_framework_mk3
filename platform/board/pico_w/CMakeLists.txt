# board_pico_w: board support for raspberry pi pico w
add_library(board_pico_w INTERFACE)
function(board_pico_w_select)

endfunction()

light_add_board(pico_w rp2040 board_pico_w_select)
                  
target_include_directories(
    board_pico_w
    INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/include
)
 
target_sources(
    board_pico_w
    INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/src/board.c
)
