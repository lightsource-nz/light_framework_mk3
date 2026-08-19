#ifndef _LIGHT_CLI_H
#define _LIGHT_CLI_H

#if LIGHT_PLATFORM_HAS_C11_THREADS
#include <threads.h>
#endif
#include <stdarg.h>

#define LIGHT_CLI_MAX_REF_DEPTH                 8

#define to_command(ptr) container_of(ptr, struct light_command, header)

#define LIGHT_CLI_MAX_SUBCOMMANDS               16
#define LIGHT_CLI_MAX_OPTIONS                   16
#define LIGHT_CLI_MAX_ARGS                      16
#define LIGHT_CLI_OPTION_RAWVALUE_MAX           32
#define LIGHT_CLI_OPTION_VALUE_MAX              32
#define LIGHT_CLI_MAX_COMMAND_DEPTH             LIGHT_CLI_MAX_SUBCOMMANDS

//   the command-line parser's own token limit, and so the largest argv a tokenized line may
// produce. Public because light_cli_tokenize_line() fills such an array on the caller's stack,
// and a caller sizing it independently would hand the parser more tokens than it reads
#define LIGHT_CLI_MAX_TOKENS                    64

#define LIGHT_CLI_OPTION                        0
#define LIGHT_CLI_SWITCH                        1
struct light_cli_option {
        struct light_command *command;
        uint8_t type;
        const char code;
        const uint8_t *name;
        const uint8_t *description;
};
struct light_cli_invocation;
struct light_command {
        struct light_command *parent;
        struct light_object header;
        const uint8_t *short_name;
        const uint8_t *full_name;
        const uint8_t *description;
        struct light_cli_invocation_result (*handler)(struct light_cli_invocation *);
        uint8_t arg_min;
        uint8_t arg_max;
        uint8_t option_count;
        uint8_t child_count;
        struct light_cli_option *option[LIGHT_CLI_MAX_OPTIONS];
        struct light_command *child[LIGHT_CLI_MAX_SUBCOMMANDS];
};
struct light_cli_option_value {
        const struct light_cli_option *option;
        const uint8_t *value;
};
struct light_cli_invocation {
        struct light_command *target;
        uint8_t option_count;
        struct light_cli_option_value option[LIGHT_CLI_MAX_OPTIONS];
        uint8_t args_bound;
        const uint8_t *arg[LIGHT_CLI_MAX_ARGS];
};
#define LIGHT_CLI_RESULT_SUCCESS        0
#define LIGHT_CLI_RESULT_ALIAS          1
#define LIGHT_CLI_RESULT_ERROR          2
struct light_cli_invocation_result {
        uint8_t code;
        union value {
                struct light_command *command;
        } value;
};
#define Result_Success (struct light_cli_invocation_result) {.code = LIGHT_CLI_RESULT_SUCCESS}
#define Result_Error (struct light_cli_invocation_result) {.code = LIGHT_CLI_RESULT_ERROR}
#define Result_Alias(target) \
        (struct light_cli_invocation_result) { \
                .code = LIGHT_CLI_RESULT_ALIAS, \
                .value.command = target \
        }

extern struct lobj_type ltype_cli_command;
// static command max-args value is determined at load time by the size of .arg_name
#define Light_Command_Static(_name, _parent, _desc, _handler, _arg_min, _arg_max, ...) \
        { \
                /*   Static_RO, not plain RO: these live in file-scope storage the program did
                 * not allocate, and ltype_cli_command carries a release hook that calls
                 * light_free(). Without the static flag, a put() reaching zero would hand that
                 * storage to free() -- see light_object_put_reg() */ \
                .header = Light_Object_Static_RO("light_cmd:"_name, NULL, &ltype_cli_command), \
                .short_name = _name, \
                .parent = _parent, \
                .description = _desc, \
                .handler = _handler, \
                .arg_min = _arg_min, \
                .arg_max = _arg_max, \
                .option = { __VA_ARGS__ } \
        }

// FIXME the arg ordering on these macros is fucking wack
#define Light_Command_Option_Type(name, command, type, code, description) \
        { command, type, code, name, description }

#define Light_Command_Switch(command, code, name, description) \
        Light_Command_Option_Type(name, command, LIGHT_CLI_SWITCH, code, description)

#define Light_Command_Option(command, code, name, description) \
        Light_Command_Option_Type(name, command, LIGHT_CLI_OPTION, code, description)

#define Light_Command_Declare(sym_name, parent) \
        extern struct light_command sym_name

extern void light_cli__autoload_command(void *object);
extern void light_cli__autoload_option(void *object);

//   NO __static_descriptor on the command itself. On RP2040/RP2350 that attribute expands to
// __in_flash(".descriptors"), and light_cli_register_command() MUTATES the command at load
// time -- it writes parent, full_name, parent->child[] and parent->child_count. Those stores
// cannot land in read-only XIP flash, so the command has to sit in RAM. Every other port
// defines __static_descriptor as nothing, so this only ever mattered on RP2.
//
//   the autoload record below keeps __static_object: that one really is const, and belongs in
// the .light.static section the framework walks at init
#define Light_Command_Define(sym_name, parent, name, description, handler, _arg_min, _arg_max, ...) \
        struct light_command sym_name = \
                Light_Command_Static(name, parent, description, handler, _arg_min, _arg_max, __VA_ARGS__); \
        static const __static_object struct light_static_object autoload_## sym_name = \
                Light_Static_Object(&sym_name, light_cli__autoload_command);

#define Light_Command_Option_Declare(sym_name, command) \
        extern struct light_cli_option sym_name

// no __static_descriptor here either, and for the same reason as Light_Command_Define above:
// light_cli_register_option_ctx() writes into the option's owning command, and an option in
// flash on RP2 could not participate
#define Light_Command_Option_Type_Define(sym_name, command, type, name, code, description) \
        struct light_cli_option sym_name = \
                        Light_Command_Option_Type(name, command, type, code, description); \
        const struct light_static_object __static_object autoload_## sym_name = \
                Light_Static_Object(&sym_name, light_cli__autoload_option)
#define Light_Command_Option_Define(sym_name, command, name, code, description) \
                Light_Command_Option_Type_Define(sym_name, command, LIGHT_CLI_OPTION, name, code, description)
#define Light_Command_Switch_Define(sym_name, command, name, code, description) \
                Light_Command_Option_Type_Define(sym_name, command, LIGHT_CLI_SWITCH, name, code, description)

extern struct light_command root_command;

// called at module load-time by framework
extern void light_cli_init();

static inline const uint8_t *light_cli_command_get_full_name(struct light_command *command)
{
        return command->full_name;
}
static inline const struct light_command *light_cli_command_get_parent(struct light_command *command)
{
        return command->parent;
}
static inline const uint8_t *light_cli_get_object_id(struct light_command *command)
{
        return light_object_get_name(&command->header);
}
static inline const uint8_t *light_cli_command_get_short_name(struct light_command *command)
{
        return command->short_name;
}
static inline const uint8_t *light_cli_command_get_description(struct light_command *command)
{
        return command->description;
}

static inline const char light_cli_option_get_code(struct light_cli_option *option)
{
        return option->code;
}
static inline const uint8_t *light_cli_option_get_name(struct light_cli_option *option)
{
        return option->name;
}
static inline uint8_t light_cli_option_get_type(struct light_cli_option *option)
{
        return option->type;
}
static inline const uint8_t *light_cli_option_get_description(struct light_cli_option *option)
{
        return option->description;
}

extern struct light_cli_option *light_cli_find_command_option(
                                struct light_command *command, const uint8_t *name);
static inline struct light_cli_option *light_cli_find_option(const uint8_t *name)
{
        return light_cli_find_command_option(NULL, name);
}

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

// command and option API
extern struct light_command *light_cli_create_command(
                                struct light_command *parent,
                                const uint8_t *name,
                                const uint8_t *description,
                                struct light_cli_invocation_result (*handler)(struct light_cli_invocation *));
extern void light_cli_register_command(
                                struct light_command *parent,
                                struct light_command *command);

extern struct light_cli_option *light_cli_create_option_ctx(
                                struct light_command *parent,
                                const uint8_t code,
                                const uint8_t *name,
                                bool arg,
                                const uint8_t *description);

static inline struct light_cli_option *light_cli_create_option(
                                const uint8_t code,
                                const uint8_t *name,
                                bool arg,
                                const uint8_t *description)
{
        return light_cli_create_option_ctx(NULL, code, name, arg, description);
}
extern void light_cli_register_option_ctx(
                                struct light_command *parent,
                                struct light_cli_option *option);

static inline void light_cli_register_option(
                                struct light_cli_option *option)
{
        return light_cli_register_option_ctx(NULL, option);
};
static inline struct light_cli_option *light_cli_create_switch_ctx(
                                struct light_command *parent,
                                const uint8_t code,
                                const uint8_t *name,
                                const uint8_t *description)
{
        return light_cli_create_option_ctx(parent, code, name, false, description);
}
static inline struct light_cli_option *light_cli_create_switch(
                                const uint8_t code,
                                const uint8_t *name,
                                const uint8_t *description)
{
        return light_cli_create_switch_ctx(NULL, code, name, description);
}
extern struct light_command *light_cli_find_command(
                                struct light_command *parent, const uint8_t *name);

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

//   writes a usage summary for `command` -- its own usage line and description, then its
// subcommands and its options -- to light_stream_stdout. Through the stream layer rather than
// printf so it stays ordered with everything else the command tree logs, and so it works on a
// target whose console is not stdio at all
extern void light_cli_print_command_help(struct light_command *command);

#endif