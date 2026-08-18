#   The only executable this project builds for the weact_stm32f446 target, and therefore the
# only thing that actually exercises the STM32F446 port: every light_framework module is an
# INTERFACE library, so a build with no executable compiles nothing and reports success.
#
#   Not registered with add_test(): it is a cross-compiled firmware image, which the host
# cannot run. What it verifies is the link -- see the section assertions below.

add_executable(demo_weact_stm32f446)

target_sources(demo_weact_stm32f446 PUBLIC ${CMAKE_CURRENT_LIST_DIR}/src/demo_weact_stm32f446.c)

target_link_libraries(
        demo_weact_stm32f446 PUBLIC
        light_core
)

#   goes through the property hook rather than calling light_cmsis_add_extra_outputs()
# directly -- see demo_blackpill's CMakeLists for what this proves if it fires
light_target_set_property(demo_weact_stm32f446 application_target 1)
