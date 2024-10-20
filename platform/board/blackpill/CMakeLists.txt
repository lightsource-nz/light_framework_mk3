# board_blackpill: board support for blackpill plus
add_library(board_blackpill INTERFACE)

function(board_blackpill_select)

endfunction()

light_add_board(blackpill stm32f411 board_blackpill_select)
                  
target_include_directories(
    board_blackpill
    INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/include
)
 
target_sources(
    board_blackpill
    INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/src/board.c
)
