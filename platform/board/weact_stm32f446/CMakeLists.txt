# board_weact_stm32f446: board support for the WeAct STM32F446 core board
add_library(board_weact_stm32f446 INTERFACE)

function(board_weact_stm32f446_select)

endfunction()

light_add_board(weact_stm32f446 stm32f446 board_weact_stm32f446_select)

target_include_directories(
    board_weact_stm32f446
    INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/include
)

target_sources(
    board_weact_stm32f446
    INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/src/board.c
)
