#   the only executable a MiniSTM32H7xx build produces, and so the only thing that compiles
# the STM32H743 port at all -- every light_framework module is an INTERFACE library.
#
#   Not registered with add_test(): it is a cross-compiled firmware image the host cannot run.
# What it verifies is the link, and then the hardware.

add_executable(demo_mini_stm32h7)

target_sources(demo_mini_stm32h7 PUBLIC ${CMAKE_CURRENT_LIST_DIR}/src/demo_mini_stm32h7.c)

target_link_libraries(
        demo_mini_stm32h7 PUBLIC
        light_core
)

# .bin/.hex and the size breakdown, through the property hook that cmake/cmsis.cmake installs
light_target_set_property(demo_mini_stm32h7 application_target 1)
