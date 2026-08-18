/*
 *  board.c
 *  board support code for the WeAct STM32F446 core board
 *
 *  authored by Alex Fulton
 *  created august 2026
 *
 */

/* no board-level init needed beyond what light_platform_init() already does -- see
 * bluepill_plus's board.c for why: LED/button pins live in light_board.h, and console/GPIO
 * setup is the stm32f446 chip port's job */
