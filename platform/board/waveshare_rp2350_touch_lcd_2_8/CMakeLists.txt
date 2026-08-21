# board_waveshare_rp2350_touch_lcd_2.8: board support for Waveshare RP2350-Touch-LCD-2.8
# NOTE: this target's name must be exactly "board_${LIGHT_BOARD}" -- light_select_board()
# in light_platform.cmake hardcodes that naming convention (LIGHT_BOARD_LIB =
# board_${BOARD}) to find and link whichever board is selected, dot included
add_library(board_waveshare_rp2350_touch_lcd_2.8 INTERFACE)
function(board_ws_touch28_select)

endfunction()

light_add_board(waveshare_rp2350_touch_lcd_2.8 rp2350 board_ws_touch28_select)

target_include_directories(
    board_waveshare_rp2350_touch_lcd_2.8
    INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/include
)

target_sources(
    board_waveshare_rp2350_touch_lcd_2.8
    INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/src/board.c
)
