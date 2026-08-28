#ifndef _LIGHT_CLI_H
#define _LIGHT_CLI_H

/*
 *  light_cli/light_cli.h
 *  the command LINE: tokenizing it, parsing it against the command tree, dispatching it, and
 *  the queue that lets a console feed lines in without running them inline.
 *
 *  the tree itself -- struct light_command, Light_Command_Define() and registration -- is in
 *  light_core/light_command.h now, because Light_Application_Define() declares a root command
 *  and so needs those macros before light_cli exists. See that file's own note. This header
 *  includes it, so nothing that used to include only light_cli.h has to change.
 */
#include <light.h>
#include <light_command.h>

#if LIGHT_PLATFORM_HAS_C11_THREADS
#include <threads.h>
#endif
#include <stdarg.h>

// how far light_cli_dispatch_command_line() will follow a chain of Result_Alias() before giving
// up. Purely a dispatch concern, so it stayed behind when the tree moved to light_command.h
#define LIGHT_CLI_MAX_REF_DEPTH                 8


// called at module load-time by framework
extern void light_cli_init();


extern const uint8_t *light_cli_invocation_get_arg_value(struct light_cli_invocation *invoke, uint8_t index);
extern const uint8_t *light_cli_invocation_get_option_value(struct light_cli_invocation *invoke, const uint8_t *option_name);
static inline bool light_cli_invocation_option_is_set(struct light_cli_invocation *invoke, const uint8_t *option_name)
{
        return light_cli_invocation_get_option_value(invoke, option_name) != NULL;
}
static inline bool light_cli_invocation_get_switch_value(struct light_cli_invocation *invoke, const uint8_t *option_name)
{
        return light_cli_invocation_option_is_set(invoke, option_name);
}

// called at application load-time by framework
extern uint8_t light_cli_process_command_line(struct light_command *root, struct light_cli_invocation *invoke, int argc, char *argv[]);
extern uint8_t light_cli_dispatch_command_line(struct light_cli_invocation *invoke);


/*   RUNNING A COMMAND LINE THAT ARRIVES AS A STRING, rather than as an argv at process launch.
 * A REPL prompt, a batch script and a command typed at a serial console are all the same
 * problem, and none of them could use the launch-time path: light_cli_process_command_line()
 * wants a tokenized argv, and dispatch lived inside a one-shot task that by construction ran
 * exactly once per process.
 *
 *   DELIBERATELY ONLY THE REUSABLE HALF. Where lines come from, when to prompt, what ends a
 * session and whether a failure should stop a script all depend on the line source -- a
 * terminal, a file, a UART -- so the loop belongs to the application. font-crusher's
 * `crush console` is one such loop. Everything declared here is string work plus the
 * framework's own stream layer: no stdio, no isatty, nothing a target build cannot compile.
 */

//   splits `line` into tokens IN PLACE, writing at most argv_max pointers into argv and the
// count to *argc_out. `line` must be writable and must outlive every use of argv, because the
// tokens are pointers into it, not copies.
//
//   Quoting: "..." and '...' group whitespace and are removed from the token. THERE ARE NO
// BACKSLASH ESCAPES, which is a decision rather than an omission -- on Windows the backslash is
// the path separator, so treating it as an escape would quietly eat the separators out of every
// path typed at the prompt. The cost is that a literal quote cannot appear inside a quoted
// token, which no path needs.
//   A '#' STARTING A TOKEN comments out the remainder of the line. Only at the start of a
// token, and only unquoted, so a '#' inside a word or inside quotes stays an ordinary
// character.
//   Returns LIGHT_OK, or LIGHT_INVALID for an unterminated quote or more than argv_max tokens
// (both logged). A blank or comment-only line is LIGHT_OK with *argc_out == 0.
extern uint8_t light_cli_tokenize_line(uint8_t *line, char *argv[], uint8_t argv_max, uint8_t *argc_out);

//   tokenizes, parses and dispatches one command line against `root`, which must be a NAMED
// command -- an application passes its own root command (crush passes cmd_crush), whose name
// becomes the synthetic argv[0] the parser matches its first token against. Lines are therefore
// typed without the program name ("font list"), though typing it anyway is accepted.
//   Returns LIGHT_OK if a command ran and reported success, or if the line was blank or a
// comment. LIGHT_INVALID if the line could not be parsed or the command reported failure; the
// reason is already logged by whichever layer found it, so a caller only needs the verdict.
extern uint8_t light_cli_run_line(struct light_command *root, uint8_t *line);

//   queues a command line for cli_task() to run on a later tick, instead of the caller running
// it (and everything after it) inline. This is what makes a console safe on a single-core
// target: cli_task() is a periodic task now, and drains at most ONE queued line per call (see
// its own comment), so a session feeding lines through here cannot starve every other periodic
// task the way a loop calling light_cli_run_line() directly, from inside a command handler,
// would.
//   backed by a light_stream_mqueue -- the same fixed-size, no-heap, lock-protected ring buffer
// light_stream's own log queues use -- rather than a second one written from scratch. `root` is
// remembered for every line currently queued, since every command line running through here
// belongs to the one application in this process; queueing from more than one root is not a
// case that exists yet.
//   Returns false, queueing nothing, for a NULL `line` or when the queue already holds
// LIGHT_STREAM_MQUEUE_DEPTH lines. Never blocks: an application feeding this from its own
// periodic task must not stall waiting for cli_task() to catch up.
//   A LINE LONGER THAN LIGHT_STREAM_MAX_MSG_LENGTH - 1 CHARACTERS IS TRUNCATED, NOT REFUSED, and
// still returns true -- the same treatment an over-long message gets from any other queue of
// this type. So a true return means the line was accepted, not that all of it was: a caller
// whose lines can be that long has to check the length itself.
extern bool light_cli_queue_line(struct light_command *root, const uint8_t *line);

#endif