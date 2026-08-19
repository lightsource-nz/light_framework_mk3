#   HOST ONLY, like the other suites. Nothing in line.c is host-specific, but the binary still
# has to run on the machine doing the building. The guard lives in the parent CMakeLists so this
# file is never reached on a target build.
add_executable(test_light_cli_line ${CMAKE_CURRENT_LIST_DIR}/test_light_cli_line.c)

target_link_libraries(test_light_cli_line PRIVATE light_core light_cli)

#   one CTest case per test function, as in the other suites: a failure names itself in the
# ctest summary rather than being one line inside a wall of output.
#
#   KEEP IN SYNC with test_cases[] in test_light_cli_line.c. An unknown name exits non-zero
# rather than passing quietly, so a mismatch surfaces as a failing test rather than as silent
# under-testing; `test_light_cli_line --list` prints the names the binary actually has.
set(LIGHT_CLI_LINE_TEST_CASES
        tokenize_splits_on_whitespace
        tokenize_collapses_runs_of_whitespace
        tokenize_blank_line_yields_nothing
        tokenize_quotes_group_and_are_removed
        tokenize_quotes_may_open_mid_token
        tokenize_other_quote_is_literal_inside
        tokenize_keeps_backslashes
        tokenize_comment_ends_the_line
        tokenize_hash_inside_a_token_is_literal
        tokenize_unterminated_quote_is_an_error
        tokenize_respects_argv_max
        tokenize_rejects_bad_arguments
        run_line_dispatches_a_subcommand
        run_line_dispatches_a_nested_subcommand
        run_line_binds_arguments_and_options
        run_line_binds_options_by_short_code
        run_line_binds_a_quoted_argument
        run_line_accepts_the_program_name
        run_line_blank_and_comment_lines_run_nothing
        run_line_does_not_carry_state_between_lines
        run_line_reports_a_failing_handler
        run_line_reports_an_unknown_option
        run_line_reports_a_command_with_no_handler
        run_line_rejects_a_line_it_cannot_tokenize
        run_line_rejects_bad_arguments
        help_prints_every_command_shape
        queue_line_runs_on_a_later_tick
        queue_line_runs_one_line_per_tick
        cli_task_is_idle_with_an_empty_queue
        queue_line_reports_a_full_queue
        queue_line_truncates_an_over_long_line
        queue_line_survives_a_failing_command
        queue_line_refuses_a_null_line
        queue_line_defers_validation_to_the_tick
)
foreach(_case ${LIGHT_CLI_LINE_TEST_CASES})
        add_test(NAME light_cli_line.${_case} COMMAND test_light_cli_line ${_case})
endforeach()
