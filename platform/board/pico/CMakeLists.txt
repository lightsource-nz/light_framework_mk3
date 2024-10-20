# board_pico_w: board support for raspberry pi pico
add_library(board_pico INTERFACE)
function(board_pico_select)

endfunction()

light_add_board(pico rp2040 board_pico_select)
                  
target_include_directories(
    board_pico
    INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/include
)
 
target_sources(
    board_pico
    INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/src/board.c
)
