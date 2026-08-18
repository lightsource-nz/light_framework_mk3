#   The only executable this project builds for the bluepill_plus target, and therefore the
# only thing that actually exercises the STM32F103 port: every light_framework module is an
# INTERFACE library, so a build with no executable compiles nothing and reports success.
#
#   Not registered with add_test(): it is a cross-compiled firmware image, which the host
# cannot run. What it verifies is the link -- see the section assertions below.

add_executable(demo_bluepill_plus)

target_sources(demo_bluepill_plus PUBLIC ${CMAKE_CURRENT_LIST_DIR}/src/demo_bluepill_plus.c)

target_link_libraries(
        demo_bluepill_plus PUBLIC
        light_core
)

#   goes through the property hook rather than calling light_cmsis_add_extra_outputs()
# directly -- see demo_blackpill's CMakeLists for what this proves if it fires
light_target_set_property(demo_bluepill_plus application_target 1)
