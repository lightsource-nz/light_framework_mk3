# board_bluepill_plus: board support for bluepill plus
add_library(board_bluepill_plus INTERFACE)

function(board_bluepill_plus_select)

endfunction()

light_add_board(bluepill_plus stm32f103 board_bluepill_plus_select)
                  
target_include_directories(
    board_bluepill_plus
    INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/include
)
 
target_sources(
    board_bluepill_plus
    INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/src/board.c
)
