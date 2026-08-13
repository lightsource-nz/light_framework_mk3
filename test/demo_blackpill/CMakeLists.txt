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

#   called directly rather than through light_target_set_property(demo_blackpill
# application_target 1), which is what the hook in cmake/cmsis.cmake was written for. That
# function is broken: it calls light_register_common_scope_var() and
# light_promote_common_scope_vars(), neither of which exists anywhere in the project, so it
# fails with "Unknown CMake command" the first time anything uses it.
#   Every RP2 application does the same thing for the same reason -- see the direct
# pico_add_extra_outputs() calls in screen-test's apps, which is why the hook has never been
# exercised. The hook stays wired up in cmsis.cmake so it starts working if that is ever fixed
light_cmsis_add_extra_outputs(demo_blackpill)
