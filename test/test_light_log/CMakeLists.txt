#   HOST ONLY: these run the real light_core and read back the text it formatted, so the binary
# has to execute on the machine doing the building. The guard lives in the parent CMakeLists so
# this file is never reached on a target build.
add_executable(test_light_log ${CMAKE_CURRENT_LIST_DIR}/test_light_log.c)

target_link_libraries(test_light_log PRIVATE light_core)

#   one CTest case per test function, as in the other suites: a failure names itself in the
# ctest summary rather than being one line inside a wall of output.
#
#   KEEP IN SYNC with test_cases[] in test_light_log.c. An unknown name here exits non-zero
# rather than passing quietly, so a mismatch surfaces as a failing test rather than as silent
# under-testing; `test_light_log --list` prints the names the binary actually has.
set(LIGHT_LOG_TEST_CASES
        format_carries_level_and_function
        format_expands_arguments
        format_ends_with_one_newline
        each_level_is_named
        long_message_is_truncated_to_fit
        truncated_message_keeps_its_newline
        long_function_name_is_bounded
        empty_message_is_well_formed
        stays_within_bounds_for_a_normal_message
        stays_within_bounds_for_a_long_message
        stays_within_bounds_for_a_long_function_name
        stays_within_bounds_for_tiny_buffers
)
foreach(_case ${LIGHT_LOG_TEST_CASES})
        add_test(NAME light_log.${_case} COMMAND test_light_log ${_case})
endforeach()
