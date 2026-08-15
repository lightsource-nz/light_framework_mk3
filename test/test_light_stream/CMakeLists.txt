#   HOST ONLY, like the other suites. The guard lives in the parent CMakeLists so this file is
# never reached on a target build.
add_executable(test_light_stream ${CMAKE_CURRENT_LIST_DIR}/test_light_stream.c)

target_link_libraries(test_light_stream PRIVATE light_core)

#   one CTest case per test function, as in the other suites: a failure names itself in the
# ctest summary rather than being one line inside a wall of output.
#
#   KEEP IN SYNC with test_cases[] in test_light_stream.c. An unknown name here exits non-zero
# rather than passing quietly, so a mismatch surfaces as a failing test rather than as silent
# under-testing; `test_light_stream --list` prints the names the binary actually has.
set(LIGHT_STREAM_TEST_CASES
        a_new_queue_is_empty_and_not_full
        try_get_on_an_empty_queue_reports_failure
        messages_come_back_in_order
        the_text_survives_the_round_trip
        is_full_becomes_true_at_the_depth
        a_full_queue_still_returns_everything
        the_buffer_wraps_and_keeps_order
        peek_does_not_consume
        advance_consumes_exactly_one
        flush_returns_on_an_empty_queue
        every_log_level_is_named
        every_error_code_is_named
        every_run_mode_is_named
)
foreach(_case ${LIGHT_STREAM_TEST_CASES})
        add_test(NAME light_stream.${_case} COMMAND test_light_stream ${_case})
endforeach()
