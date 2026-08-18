/*
 *  board.c
 *  board support code for WeAct BluePill+
 *
 *  authored by Alex Fulton
 *  created march 2024
 *
 */

/* no board-level init needed beyond what light_platform_init() already does: the LED and SWD
 * pin assignments live in light_board.h, and console/GPIO setup is the stm32f103 chip port's
 * job (see module/light_core_chip_stm32f103), since none of it is specific to this particular
 * board layout */
