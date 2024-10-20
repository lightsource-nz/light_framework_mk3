# board_mini_stm32h7: board support for mini_stm32h7
add_library(board_mini_stm32h7 INTERFACE)

function(board_mini_stm32h7_select)

endfunction()

light_add_board(mini_stm32h7 stm32h743 board_mini_stm32h7_select)
                  
target_include_directories(
    board_mini_stm32h7
    INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/include
)
 
target_sources(
    board_mini_stm32h7
    INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/src/board.c
)
