
add_executable(demo_cli)

target_sources(demo_cli PUBLIC ${CMAKE_CURRENT_LIST_DIR}/src/demo_cli.c)

target_link_libraries(
    demo_cli PUBLIC
    light_core
    light_cli
)

if(LIGHT_SYSTEM STREQUAL PICO_SDK AND NOT PICO_NO_HARDWARE)
        pico_enable_stdio_semihosting(demo_cli 1)
        target_link_libraries(demo_cli PUBLIC pico_stdlib)
endif()

# regression test for light_cli_find_command_option() (module/light_cli/src/command.c): it used
# to return on the first NON-matching option instead of the matching one (an inverted strncmp()
# check), so option lookups on any command with 2+ registered options could silently resolve to
# the wrong option object. this went unnoticed for a while because looking up an option that WAS
# passed on the command line often still worked -- the same wrong resolution happens both when
# the parser binds "--optname value" to an option object, and later when a handler looks that
# option up by name, so the two wrongs can cancel out. it does NOT cancel out for an option that
# is registered but never passed: demo_command's 'gamma' option exercises exactly that case,
# which is what actually broke font-crusher's 'display add' (an unset '--pixel-depth' resolved
# to whatever value 'display add' actually received via '--dimension', since neither option's
# name matched the first entry in the array)
add_test(
        NAME demo_cli__option_lookup
        COMMAND $<TARGET_FILE:demo_cli> --alpha foo --beta bar
)
# 'gamma' is deliberately never passed above -- if the lookup logic regresses, gamma_set would
# come back true (bound, incorrectly, to whichever option's value the buggy lookup lands on)
set_property(TEST demo_cli__option_lookup PROPERTY PASS_REGULAR_EXPRESSION
        "option values: alpha='foo' beta='bar' gamma_set=false")
