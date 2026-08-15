#   HOST ONLY, like the other suites. The guard lives in the parent CMakeLists so this file is
# never reached on a target build.
#
#   a separate binary from test_light_stream rather than more cases in it: this one defines a
# capture stream and brings it up with light_stream_init(), while that one drives a bare
# mqueue with no stream at all. Keeping them apart is what lets the queue tests stay free of
# any stream state.
add_executable(test_light_stream_message ${CMAKE_CURRENT_LIST_DIR}/test_light_stream_message.c)

target_link_libraries(test_light_stream_message PRIVATE light_core)

#   KEEP IN SYNC with test_cases[] in test_light_stream_message.c. An unknown name here exits
# non-zero rather than passing quietly, so a mismatch surfaces as a failing test rather than as
# silent under-testing; `test_light_stream_message --list` prints what the binary has.
set(LIGHT_STREAM_MESSAGE_TEST_CASES
        sync_reaches_the_handler_immediately
        fast_queues_rather_than_writing
        formatting_happens_at_call_time
        a_long_message_is_truncated
        queued_messages_keep_their_order
        messages_carry_their_flags
        background_logging_mode_round_trips
        output_lock_round_trips
)
foreach(_case ${LIGHT_STREAM_MESSAGE_TEST_CASES})
        add_test(NAME light_stream_message.${_case} COMMAND test_light_stream_message ${_case})
endforeach()
