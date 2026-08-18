/*
 *  test_light_cli_line.c
 *  the runtime command-line half of light_cli: tokenizing a typed line, and dispatching it
 *
 *  HOST ONLY, like the other suites, though nothing under test is host-specific -- line.c is
 *  string work over the framework's own stream layer, with no stdio and no platform calls.
 *
 *  WHY THIS SUITE EXISTS. light_cli could only ever run ONE command line per process: the
 *  parser wanted an argv the C runtime had already tokenized, and dispatch was the body of a
 *  one-shot task. A console -- font-crusher's `crush console`, or a command typed at a serial
 *  port -- needs both halves callable repeatedly, and the two failure modes that introduces are
 *  invisible to any single-command test:
 *
 *    - INVOCATION STATE CARRYING OVER between lines. light_cli_process_command_line() APPENDS to
 *      option[] and arg[] and trusts the counts it finds, so a reused invocation accumulates the
 *      previous line's arguments underneath this line's. That is correct-looking on line one and
 *      wrong on every line after it, which is the shape of bug a one-shot path cannot have.
 *      run_line_does_not_carry_state_between_lines is the case for it.
 *    - A NULL HANDLER. Any intermediate node in a command tree is entitled to have none, and
 *      calling through it was previously a fault rather than a diagnostic. Typing an incomplete
 *      command at a prompt ("font" with no subcommand) reaches that path immediately, where a
 *      shell invocation practically never did.
 *
 *  WHAT IS DELIBERATELY NOT ASSERTED. light_cli_print_command_help() writes through the stream
 *  layer, so checking its text would mean capturing stdout out from under a background worker
 *  thread. The case here calls it across the shapes that differ structurally -- a node with
 *  children, a leaf with options, and the anonymous root_command whose name is NULL -- and
 *  asserts only that it returns. That covers the pointer handling, which is what would fault;
 *  the formatting is checked by reading it.
 */
#include <light.h>
// declares the light_cli module descriptor the application below names as a dependency
#include <module/mod_light_cli.h>
#include <light_cli.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void _test_app_event(const struct light_module *module, uint8_t event, void *arg) {}
static uint8_t _test_app_main(struct light_application *app) { return LF_STATUS_RUN; }
//   light_cli is a dependency so its MODULE_LOAD runs and the command tree below is registered
// by the framework's own static-object walk, rather than by this file reaching for
// light_cli_register_command() and testing a registration path no application uses
Light_Application_Define(test_light_cli_line, _test_app_event, _test_app_main,
                                                                &light_cli,
                                                                &light_core);

static int failures;

#define CHECK(cond, ...) do { \
        if(!(cond)) { \
                failures++; \
                printf("  FAIL %s:%d: ", __func__, __LINE__); \
                printf(__VA_ARGS__); \
                printf("\n"); \
        } \
} while(0)

// --- the command tree under test -------------------------------------------------------------

//   what the handlers below record, so a case can assert what the dispatch actually delivered
// rather than only that it returned success
static const uint8_t *seen_command;
static uint8_t seen_argc;
static const uint8_t *seen_arg[LIGHT_CLI_MAX_ARGS];
static const uint8_t *seen_opt;
static bool seen_switch;
static uint8_t handler_calls;

static void record_reset(void)
{
        seen_command = NULL;
        seen_argc = 0;
        memset(seen_arg, 0, sizeof(seen_arg));
        seen_opt = NULL;
        seen_switch = false;
        handler_calls = 0;
}
static struct light_cli_invocation_result record(struct light_cli_invocation *invoke)
{
        handler_calls++;
        seen_command = light_cli_command_get_short_name(invoke->target);
        seen_argc = invoke->args_bound;
        for(uint8_t i = 0; i < invoke->args_bound && i < LIGHT_CLI_MAX_ARGS; i++)
                seen_arg[i] = light_cli_invocation_get_arg_value(invoke, i);
        seen_opt = light_cli_invocation_get_option_value(invoke, "opt");
        seen_switch = light_cli_invocation_get_switch_value(invoke, "sw");
        return Result_Success;
}
static struct light_cli_invocation_result do_fail(struct light_cli_invocation *invoke)
{
        handler_calls++;
        return Result_Error;
}
//   'branch' deliberately has NO HANDLER (a NULL where every other command has a function),
// because that is what an intermediate node in a real tree looks like -- `crush font` exists to
// own `crush font add`, not to do anything itself
Light_Command_Define(cmd_test, NULL, "test_cli", "root of the test command tree", record, 0, 2);
Light_Command_Define(cmd_test_echo, &cmd_test, "echo", "records what it was given", record, 0, 3);
Light_Command_Define(cmd_test_fail, &cmd_test, "fail", "always reports failure", do_fail, 0, 0);
Light_Command_Define(cmd_test_branch, &cmd_test, "branch", "takes a subcommand and does nothing itself", NULL, 0, 0);
Light_Command_Define(cmd_test_leaf, &cmd_test_branch, "leaf", "records what it was given", record, 0, 1);

Light_Command_Option_Define(opt_test_opt, &cmd_test_echo, "opt", 'o', "an option that takes a value");
Light_Command_Switch_Define(sw_test_sw, &cmd_test_echo, "sw", 's', "a switch that takes none");

// --- helpers ---------------------------------------------------------------------------------

//   the tokenizer splits IN PLACE, so every case needs a writable copy -- a string literal
// would be a segfault on the first token boundary, and one shared buffer keeps that impossible
// to forget
static uint8_t buffer[512];
static uint8_t *line(const char *text)
{
        snprintf((char *)buffer, sizeof(buffer), "%s", text);
        return buffer;
}
static uint8_t tokenize(const char *text, char *argv[], uint8_t argv_max, uint8_t *argc)
{
        return light_cli_tokenize_line(line(text), argv, argv_max, argc);
}
static uint8_t run(const char *text)
{
        record_reset();
        return light_cli_run_line(&cmd_test, line(text));
}

// --- tokenizing ------------------------------------------------------------------------------

static void test_tokenize_splits_on_whitespace(void)
{
        char *argv[8];
        uint8_t argc = 0;

        CHECK(tokenize("one two three", argv, 8, &argc) == LIGHT_OK, "should tokenize");
        CHECK(argc == 3, "expected 3 tokens, got %d", argc);
        CHECK(argc == 3 && !strcmp(argv[0], "one"), "token 0 wrong");
        CHECK(argc == 3 && !strcmp(argv[1], "two"), "token 1 wrong");
        CHECK(argc == 3 && !strcmp(argv[2], "three"), "token 2 wrong");
}
static void test_tokenize_collapses_runs_of_whitespace(void)
{
        char *argv[8];
        uint8_t argc = 0;

        // leading, trailing, repeated, and tabs: all the same delimiter
        CHECK(tokenize("  one \t\t two  ", argv, 8, &argc) == LIGHT_OK, "should tokenize");
        CHECK(argc == 2, "expected 2 tokens, got %d", argc);
        CHECK(argc == 2 && !strcmp(argv[1], "two"), "token 1 wrong");
}
static void test_tokenize_blank_line_yields_nothing(void)
{
        char *argv[8];
        uint8_t argc = 1;

        CHECK(tokenize("   \t ", argv, 8, &argc) == LIGHT_OK, "a blank line is not an error");
        CHECK(argc == 0, "expected 0 tokens, got %d", argc);
}
static void test_tokenize_quotes_group_and_are_removed(void)
{
        char *argv[8];
        uint8_t argc = 0;

        //   the case this exists for: a path with a space in it, which is most of them on
        // Windows. The quotes must not survive into the token, or the value handed to fopen()
        // has quotes in it
        CHECK(tokenize("add \"my font.ttf\" 'other name'", argv, 8, &argc) == LIGHT_OK, "should tokenize");
        CHECK(argc == 3, "expected 3 tokens, got %d", argc);
        CHECK(argc == 3 && !strcmp(argv[1], "my font.ttf"), "double-quoted token wrong: '%s'", argv[1]);
        CHECK(argc == 3 && !strcmp(argv[2], "other name"), "single-quoted token wrong: '%s'", argv[2]);
}
static void test_tokenize_quotes_may_open_mid_token(void)
{
        char *argv[8];
        uint8_t argc = 0;

        // a quote is a grouping mark, not a token delimiter -- so this is ONE token
        CHECK(tokenize("--path=\"a b\"", argv, 8, &argc) == LIGHT_OK, "should tokenize");
        CHECK(argc == 1, "expected 1 token, got %d", argc);
        CHECK(argc == 1 && !strcmp(argv[0], "--path=a b"), "token wrong: '%s'", argv[0]);
}
static void test_tokenize_other_quote_is_literal_inside(void)
{
        char *argv[8];
        uint8_t argc = 0;

        // with no escape character available, this is the only way to type either quote mark
        CHECK(tokenize("\"it's here\"", argv, 8, &argc) == LIGHT_OK, "should tokenize");
        CHECK(argc == 1 && !strcmp(argv[0], "it's here"), "token wrong: '%s'", argv[0]);
}
static void test_tokenize_keeps_backslashes(void)
{
        char *argv[8];
        uint8_t argc = 0;

        //   THE REASON THERE ARE NO ESCAPES. If '\' were an escape character this token would
        // come out as "C:fontsmy.ttf", and every Windows path typed at a prompt would be quietly
        // destroyed rather than rejected
        CHECK(tokenize("C:\\fonts\\my.ttf", argv, 8, &argc) == LIGHT_OK, "should tokenize");
        CHECK(argc == 1 && !strcmp(argv[0], "C:\\fonts\\my.ttf"), "token wrong: '%s'", argv[0]);
}
static void test_tokenize_comment_ends_the_line(void)
{
        char *argv[8];
        uint8_t argc = 0;

        CHECK(tokenize("font list # everything after this is a comment", argv, 8, &argc) == LIGHT_OK,
                        "should tokenize");
        CHECK(argc == 2, "expected 2 tokens, got %d", argc);
        CHECK(tokenize("# whole line", argv, 8, &argc) == LIGHT_OK, "should tokenize");
        CHECK(argc == 0, "a comment line has no tokens, got %d", argc);
}
static void test_tokenize_hash_inside_a_token_is_literal(void)
{
        char *argv[8];
        uint8_t argc = 0;

        //   only a '#' where a TOKEN WOULD HAVE STARTED is a comment. Anything else would make
        // a colour literal or a URL fragment untypeable
        CHECK(tokenize("--colour #ff00ff", argv, 8, &argc) == LIGHT_OK, "should tokenize");
        CHECK(argc == 1, "the '#' token starts a comment, so expected 1 token, got %d", argc);
        CHECK(tokenize("--colour=#ff00ff", argv, 8, &argc) == LIGHT_OK, "should tokenize");
        CHECK(argc == 1 && !strcmp(argv[0], "--colour=#ff00ff"), "token wrong: '%s'", argv[0]);
        CHECK(tokenize("\"#quoted\"", argv, 8, &argc) == LIGHT_OK, "should tokenize");
        CHECK(argc == 1 && !strcmp(argv[0], "#quoted"), "token wrong: '%s'", argv[0]);
}
static void test_tokenize_unterminated_quote_is_an_error(void)
{
        char *argv[8];
        uint8_t argc = 0;

        //   rejected rather than silently closed at end of line. A console can then say so and
        // discard the line, instead of running a command against half a path
        CHECK(tokenize("add \"my font.ttf", argv, 8, &argc) == LIGHT_INVALID,
                        "an unterminated quote must fail");
        CHECK(tokenize("add 'other", argv, 8, &argc) == LIGHT_INVALID,
                        "an unterminated single quote must fail");
}
static void test_tokenize_respects_argv_max(void)
{
        char *argv[4];
        uint8_t argc = 0;

        //   the bound is the caller's array size, and overrunning it would write past the end of
        // a stack array in whichever console called this
        CHECK(tokenize("a b c d", argv, 4, &argc) == LIGHT_OK, "exactly argv_max tokens must fit");
        CHECK(argc == 4, "expected 4 tokens, got %d", argc);
        CHECK(tokenize("a b c d e", argv, 4, &argc) == LIGHT_INVALID, "one too many must fail");
}
static void test_tokenize_rejects_bad_arguments(void)
{
        char *argv[4];
        uint8_t argc = 0;

        CHECK(light_cli_tokenize_line(NULL, argv, 4, &argc) == LIGHT_INVALID, "NULL line must fail");
        CHECK(light_cli_tokenize_line(line("a"), NULL, 4, &argc) == LIGHT_INVALID, "NULL argv must fail");
        CHECK(light_cli_tokenize_line(line("a"), argv, 0, &argc) == LIGHT_INVALID, "zero argv_max must fail");
        CHECK(light_cli_tokenize_line(line("a"), argv, 4, NULL) == LIGHT_INVALID, "NULL argc must fail");
}

// --- running a line -------------------------------------------------------------------------

static void test_run_line_dispatches_a_subcommand(void)
{
        //   typed WITHOUT the program name, which is the whole point of the synthetic argv[0]:
        // the parser matches token 0 against the children of the root's parent
        CHECK(run("echo") == LIGHT_OK, "should run");
        CHECK(handler_calls == 1, "expected 1 handler call, got %d", handler_calls);
        CHECK(seen_command && !strcmp((const char *)seen_command, "echo"),
                        "wrong command dispatched: '%s'", seen_command ? (const char *)seen_command : "(null)");
}
static void test_run_line_dispatches_a_nested_subcommand(void)
{
        CHECK(run("branch leaf x") == LIGHT_OK, "should run");
        CHECK(seen_command && !strcmp((const char *)seen_command, "leaf"), "wrong command dispatched");
        CHECK(seen_argc == 1 && seen_arg[0] && !strcmp((const char *)seen_arg[0], "x"), "argument not bound");
}
static void test_run_line_binds_arguments_and_options(void)
{
        CHECK(run("echo --opt value first second") == LIGHT_OK, "should run");
        CHECK(seen_opt && !strcmp((const char *)seen_opt, "value"),
                        "option value wrong: '%s'", seen_opt ? (const char *)seen_opt : "(null)");
        CHECK(seen_argc == 2, "expected 2 args, got %d", seen_argc);
        CHECK(seen_argc == 2 && !strcmp((const char *)seen_arg[0], "first"), "arg 0 wrong");
        CHECK(seen_argc == 2 && !strcmp((const char *)seen_arg[1], "second"), "arg 1 wrong");
}
static void test_run_line_binds_options_by_short_code(void)
{
        //   '-o' is '--opt' and '-s' is '--sw'. Every option in every one of these projects
        // declares a single-letter code, and until light_cli_find_command_option() grew a second
        // pass NOTHING compared against it -- the short form of every option reported "no option
        // named 'o'" and failed the parse. A help listing that prints "-o, --opt" has to be true
        CHECK(run("echo -o value -s") == LIGHT_OK, "should run");
        CHECK(seen_opt && !strcmp((const char *)seen_opt, "value"),
                        "short code did not bind the option value: '%s'",
                        seen_opt ? (const char *)seen_opt : "(null)");
        CHECK(seen_switch, "short code did not set the switch");
}
static void test_run_line_binds_a_quoted_argument(void)
{
        // the tokenizer and the parser end to end: quotes removed, one argument, spaces intact
        CHECK(run("echo \"two words\"") == LIGHT_OK, "should run");
        CHECK(seen_argc == 1, "expected 1 arg, got %d", seen_argc);
        CHECK(seen_argc == 1 && !strcmp((const char *)seen_arg[0], "two words"),
                        "argument wrong: '%s'", (const char *)seen_arg[0]);
}
static void test_run_line_accepts_the_program_name(void)
{
        //   "test_cli echo" is what documentation and shell history look like, so it is what
        // gets pasted at a prompt. It must reach the same place as "echo" rather than binding
        // its own root command's name as an argument
        CHECK(run("test_cli echo one") == LIGHT_OK, "should run");
        CHECK(seen_command && !strcmp((const char *)seen_command, "echo"), "wrong command dispatched");
        CHECK(seen_argc == 1, "expected 1 arg, got %d -- the program name must not be bound", seen_argc);
}
static void test_run_line_blank_and_comment_lines_run_nothing(void)
{
        CHECK(run("") == LIGHT_OK, "an empty line is not an error");
        CHECK(handler_calls == 0, "an empty line must not dispatch");
        CHECK(run("   ") == LIGHT_OK, "a whitespace line is not an error");
        CHECK(handler_calls == 0, "a whitespace line must not dispatch");
        CHECK(run("# a comment") == LIGHT_OK, "a comment line is not an error");
        CHECK(handler_calls == 0, "a comment line must not dispatch");
}
static void test_run_line_does_not_carry_state_between_lines(void)
{
        //   THE CASE THIS SUITE EXISTS FOR. The parser appends to the invocation's option and
        // argument arrays and trusts the counts already in them, so a reused invocation shows
        // the previous line's option still set and its arguments still bound. Every line must
        // start from a zeroed one
        CHECK(run("echo --opt sticky --sw one two") == LIGHT_OK, "first line should run");
        CHECK(seen_opt != NULL, "first line should see the option");
        CHECK(seen_switch, "first line should see the switch");
        CHECK(seen_argc == 2, "first line should bind 2 args, got %d", seen_argc);

        CHECK(run("echo solo") == LIGHT_OK, "second line should run");
        CHECK(seen_opt == NULL, "the option leaked from the previous line: '%s'",
                        seen_opt ? (const char *)seen_opt : "(null)");
        CHECK(!seen_switch, "the switch leaked from the previous line");
        CHECK(seen_argc == 1, "expected 1 arg, got %d -- arguments leaked from the previous line", seen_argc);
        CHECK(seen_argc >= 1 && !strcmp((const char *)seen_arg[0], "solo"), "arg 0 wrong");
}
static void test_run_line_reports_a_failing_handler(void)
{
        //   a command that ran and said no. The line was valid, so this is not a parse failure,
        // but a console driving a script has to be able to stop on it
        CHECK(run("fail") == LIGHT_INVALID, "a failing handler must be reported");
        CHECK(handler_calls == 1, "the handler should still have been called");
}
static void test_run_line_reports_an_unknown_option(void)
{
        CHECK(run("echo --nosuch value") == LIGHT_INVALID, "an unknown option must be reported");
        CHECK(handler_calls == 0, "nothing should be dispatched after a parse failure");
}
static void test_run_line_reports_a_command_with_no_handler(void)
{
        //   'branch' exists to own 'leaf'. Typing it alone used to call through a NULL function
        // pointer, which a shell invocation could reach too but a prompt reaches constantly
        CHECK(run("branch") == LIGHT_INVALID, "a command with no handler must be reported");
        CHECK(handler_calls == 0, "nothing should have been called");
}
static void test_run_line_rejects_a_line_it_cannot_tokenize(void)
{
        CHECK(run("echo \"unterminated") == LIGHT_INVALID, "an unterminated quote must be reported");
        CHECK(handler_calls == 0, "nothing should be dispatched from a line that would not tokenize");
}
static void test_run_line_rejects_bad_arguments(void)
{
        //   root_command is anonymous -- it is the placeholder that owns an application's real
        // root commands -- so there is no name to synthesise an argv[0] from
        CHECK(light_cli_run_line(&root_command, line("echo")) == LIGHT_INVALID,
                        "an unnamed root must be refused");
        CHECK(light_cli_run_line(NULL, line("echo")) == LIGHT_INVALID, "a NULL root must be refused");
        CHECK(light_cli_run_line(&cmd_test, NULL) == LIGHT_INVALID, "a NULL line must be refused");
}
static void test_help_prints_every_command_shape(void)
{
        //   no assertion on the text -- see the header. This covers the pointer handling across
        // the shapes that differ: children with descriptions, both kinds of option, a leaf with
        // neither, and the anonymous root whose name and description are NULL
        light_cli_print_command_help(&cmd_test);
        light_cli_print_command_help(&cmd_test_echo);
        light_cli_print_command_help(&cmd_test_leaf);
        light_cli_print_command_help(&root_command);
        light_cli_print_command_help(NULL);
        CHECK(true, "reached the end without faulting");
}

static const struct { const char *name; void (*fn)(void); } test_cases[] = {
        { "tokenize_splits_on_whitespace",            test_tokenize_splits_on_whitespace },
        { "tokenize_collapses_runs_of_whitespace",    test_tokenize_collapses_runs_of_whitespace },
        { "tokenize_blank_line_yields_nothing",       test_tokenize_blank_line_yields_nothing },
        { "tokenize_quotes_group_and_are_removed",    test_tokenize_quotes_group_and_are_removed },
        { "tokenize_quotes_may_open_mid_token",       test_tokenize_quotes_may_open_mid_token },
        { "tokenize_other_quote_is_literal_inside",   test_tokenize_other_quote_is_literal_inside },
        { "tokenize_keeps_backslashes",               test_tokenize_keeps_backslashes },
        { "tokenize_comment_ends_the_line",           test_tokenize_comment_ends_the_line },
        { "tokenize_hash_inside_a_token_is_literal",  test_tokenize_hash_inside_a_token_is_literal },
        { "tokenize_unterminated_quote_is_an_error",  test_tokenize_unterminated_quote_is_an_error },
        { "tokenize_respects_argv_max",               test_tokenize_respects_argv_max },
        { "tokenize_rejects_bad_arguments",           test_tokenize_rejects_bad_arguments },
        { "run_line_dispatches_a_subcommand",         test_run_line_dispatches_a_subcommand },
        { "run_line_dispatches_a_nested_subcommand",  test_run_line_dispatches_a_nested_subcommand },
        { "run_line_binds_arguments_and_options",     test_run_line_binds_arguments_and_options },
        { "run_line_binds_options_by_short_code",     test_run_line_binds_options_by_short_code },
        { "run_line_binds_a_quoted_argument",         test_run_line_binds_a_quoted_argument },
        { "run_line_accepts_the_program_name",        test_run_line_accepts_the_program_name },
        { "run_line_blank_and_comment_lines_run_nothing", test_run_line_blank_and_comment_lines_run_nothing },
        { "run_line_does_not_carry_state_between_lines", test_run_line_does_not_carry_state_between_lines },
        { "run_line_reports_a_failing_handler",       test_run_line_reports_a_failing_handler },
        { "run_line_reports_an_unknown_option",       test_run_line_reports_an_unknown_option },
        { "run_line_reports_a_command_with_no_handler", test_run_line_reports_a_command_with_no_handler },
        { "run_line_rejects_a_line_it_cannot_tokenize", test_run_line_rejects_a_line_it_cannot_tokenize },
        { "run_line_rejects_bad_arguments",           test_run_line_rejects_bad_arguments },
        { "help_prints_every_command_shape",          test_help_prints_every_command_shape },
};
#define TEST_CASE_COUNT (sizeof(test_cases) / sizeof(test_cases[0]))

int main(int argc, char **argv)
{
        //   the FULL framework init, unlike the other suites' light_core_impl_setup(): the code
        // under test logs its diagnostics through the stream layer, which needs its queues and
        // its worker, and the command tree above needs the static-object walk that registers it.
        // light_framework_run() is never called, so no task ever dispatches anything on its own
        light_framework_init();

        if(argc > 1 && strcmp(argv[1], "--list") == 0) {
                for(size_t i = 0; i < TEST_CASE_COUNT; i++)
                        printf("%s\n", test_cases[i].name);
                return 0;
        }

        if(argc > 1) {
                for(size_t i = 0; i < TEST_CASE_COUNT; i++) {
                        if(strcmp(argv[1], test_cases[i].name) != 0)
                                continue;
                        test_cases[i].fn();
                        // before printing the verdict: the diagnostics the cases provoked are
                        // still queued, and reading a failure without them is guesswork
                        light_stream_flush();
                        printf("%s: %s, %d failure(s)\n", test_cases[i].name,
                               failures ? "FAILED" : "PASSED", failures);
                        return failures ? 1 : 0;
                }
                // an unknown name is an error, not a silent pass: a typo in CMakeLists.txt
                // would otherwise register a test that always succeeds
                printf("FAIL: no such test case '%s'\n", argv[1]);
                return 2;
        }

        for(size_t i = 0; i < TEST_CASE_COUNT; i++)
                test_cases[i].fn();
        light_stream_flush();
        printf("%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
        return failures ? 1 : 0;
}
