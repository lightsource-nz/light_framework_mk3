#   The only executable this project builds for a bare-CMSIS target, and therefore the only
# thing that actually exercises the STM32F411 port: every light_framework module is an
# INTERFACE library, so a build with no executable compiles nothing and reports success.
#
#   Not registered with add_test(): it is a cross-compiled firmware image, which the host
# cannot run. What it verifies is the link -- see the section assertions below.

add_executable(demo_blackpill)

target_sources(demo_blackpill PUBLIC ${CMAKE_CURRENT_LIST_DIR}/src/demo_blackpill.c)

target_link_libraries(
        demo_blackpill PUBLIC
        light_core
)

#   goes through the property hook rather than calling light_cmsis_add_extra_outputs()
# directly, which is what the hook in cmake/cmsis.cmake was always meant for. It was
# unusable until the fixes in light_platform.cmake -- light_target_set_property() called two
# functions that do not exist, and the global hook list was read under a different name from
# the one it is written to -- so this is also what proves that machinery works: if the hook
# does not fire, no .bin and no .hex appear
light_target_set_property(demo_blackpill application_target 1)
