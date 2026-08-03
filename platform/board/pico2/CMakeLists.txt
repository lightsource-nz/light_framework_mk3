# board_pico2: board support for raspberry pi pico 2
add_library(board_pico2 INTERFACE)
function(board_pico2_select)

endfunction()

light_add_board(pico2 rp2350 board_pico2_select)

target_include_directories(
    board_pico2
    INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/include
)

target_sources(
    board_pico2
    INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/src/board.c
)
