/*
 *  light_cli/src/command.c
 *  functions implementing the light_cli command API
 * 
 *  authored by Alex Fulton
 *  created november 2024
 * 
 */
#include <light.h>
#include <light_cli.h>
#include <stdio.h>
#include <string.h>

#include "cli_private.h"

#define TYPE_NAME_COMMAND               "light_cli:command"

static void command_release(struct light_object *cmd);
struct lobj_type ltype_cli_command = {
        .id = TYPE_NAME_COMMAND,
        .release = command_release
};

//   we use this structure as a placeholder to store actual root-level commands,
// but it has no name and cannot be invoked
struct light_command root_command;
struct light_cli_invocation static_invoke;

//   light_cli_queue_line()'s backing store -- see that function and cli_task() for the full
// picture. A light_stream_mqueue rather than a bespoke ring buffer: it is already the
// framework's fixed-size, no-heap, lock-protected queue of text messages, and reusing it here
// is one queue implementation to get right instead of two
static light_mutex_t queue_lock;
static struct light_stream_mqueue queue;
//   the root every currently-queued line dispatches against. A single value rather than one
// per queued line because every line passing through this queue belongs to the one application
// running in this process -- see light_cli_queue_line()'s header comment
static struct light_command *queue_root;

static void command_release(struct light_object *cmd)
{
        light_free(to_command(cmd));
}
void light_cli_init()
{
        light_mutex_init(&queue_lock);
        light_stream_mqueue_init(&queue);
}
bool light_cli_queue_line(struct light_command *root, const uint8_t *line)
{
        //   NULL is refused rather than passed to snprintf("%s"), where it is undefined
        // behaviour. "queue whatever the console just read" is the natural way to call this and
        // a read that failed is the obvious way to arrive with nothing, so it is a case to
        // answer rather than one to leave to chance.
        //   MEASURED, not assumed: with this check removed, mingw's CRT does not fault -- it
        // formats the literal text "(null)", which is then queued and DISPATCHED a tick later as
        // a command line nobody typed. That is the worse of the two outcomes, because it is
        // silent; a libc that faults instead at least says something happened.
        //   `root` is deliberately NOT checked here: it cannot be validated without duplicating
        // what light_cli_run_line() already does, and there is no caller waiting on the answer
        // by the time it would be known. An unusable root costs its line a tick later, which is
        // where every other line-level failure is reported too
        if(!line) {
                light_error("refusing to queue a NULL command line");
                return false;
        }
        light_mutex_do_lock(&queue_lock);
        if(light_stream_mqueue_is_full(&queue)) {
                light_mutex_do_unlock(&queue_lock);
                return false;
        }
        queue_root = root;
        uint8_t index = queue.head;
        queue.count++;
        queue.head = (queue.head + 1) % LIGHT_STREAM_MQUEUE_DEPTH;
        queue.message[index].flags = LIGHT_MSG_FAST;
        snprintf((char *)queue.message[index].text, LIGHT_STREAM_MAX_MSG_LENGTH, "%s", (const char *)line);
        light_mutex_do_unlock(&queue_lock);
        return true;
}
#define LIGHT_CLI_COMMAND_NAME_BUFFER_SIZE      128
static const uint8_t *cli_command_get_full_name(struct light_command *command)
{
        static uint8_t buffer[LIGHT_CLI_COMMAND_NAME_BUFFER_SIZE];
        struct light_command *stack[LIGHT_CLI_MAX_COMMAND_DEPTH];
        uint8_t depth = 0;

        //   walk up to the root recording the path, BOUNDED by the size of stack[]. The loop
        // had no such bound, so a tree deeper than LIGHT_CLI_MAX_COMMAND_DEPTH wrote past the
        // end of it -- and the depth is whatever the application's command hierarchy happens
        // to be, not something this file controls
        for(struct light_command *next = command;
                        next && next != &root_command && depth < LIGHT_CLI_MAX_COMMAND_DEPTH;
                        next = next->parent) {
                stack[depth++] = next;
        }

        //   then back down again, joining with spaces, every append bounded by the space
        // actually REMAINING.
        //
        //   this used to call strncat() with `end - cursor` as its bound. strncat's third
        // argument is how many characters it may add ON TOP of what the destination already
        // holds, and it writes a terminator after those -- so the bound has to be the space
        // left, less one. `cursor` was never advanced, so `end - cursor` was the whole buffer
        // size on every call, and a long enough command path ran off the end of it. That is
        // what -Wstringop-overflow was pointing at
        size_t used = 0;
        buffer[0] = '\0';
        for(uint8_t i = depth; i > 0; i--) {
                const uint8_t *part = light_cli_command_get_short_name(stack[i - 1]);
                size_t len = strlen((const char *) part);
                size_t sep = (i > 1) ? 1 : 0;

                // +1 for the terminator, which is never part of the space a bound may use
                if(used + len + sep + 1 > sizeof(buffer)) {
                        light_warn("command name for '%s' truncated at %u characters",
                                        light_cli_command_get_short_name(command), (unsigned) used);
                        break;
                }
                memcpy(buffer + used, part, len);
                used += len;
                if(sep)
                        buffer[used++] = ' ';
                buffer[used] = '\0';
        }

        uint8_t *out = light_alloc(used + 1);
        memcpy(out, buffer, used + 1);
        return out;
}
// minimal portable replacement for POSIX basename() (<libgen.h> isn't available on bare-metal
// ARM newlib): returns a pointer to the last path component, or the whole string if it
// contains no separator. unlike POSIX basename(), never modifies or reallocates 'path'
static uint8_t *_cli_basename(uint8_t *path)
{
        if(!path) return (uint8_t *)"";
        uint8_t *out = path;
        for(uint8_t *p = path; *p; p++) {
                if(*p == '/' || *p == '\\')
                        out = p + 1;
        }
        return out;
}
void light_cli__autoload_command(void *object)
{
        struct light_command *command = (struct light_command *)object;
        light_cli_register_command(command->parent, command);
}
void light_cli__autoload_option(void *object)
{
        struct light_cli_option *option = (struct light_cli_option *)object;
        light_cli_register_option_ctx(option->command, option);
}
//   the internal command-line parser's input limit. Spelled as the public constant because
// light_cli_tokenize_line() sizes a caller's argv by it, and the two must agree: an argv longer
// than this is silently half-read by the classifier loop below
#define MAX_TOKENS              LIGHT_CLI_MAX_TOKENS
#define TOKEN_CMDARG            0
#define TOKEN_OPT_S             1
#define TOKEN_OPT_L             2
struct cli_token {
        uint8_t type;
        const uint8_t *value;
};
// uint8_t light_cli_process_command_line(
//                      struct light_command *root,
//                      struct light_cli_invocation *invoke,
//                      int argc, char *argv[] )
//
//   this routine is intended to be run once, after the command tree has been loaded,
// and performs the hard work of matching input strings to commands, arguments and
// options. it writes the resulting command invocation into the structure pointed to
// by *invoke. the target command and all options are stored as pointers to the corres-
// ponding objects in the command tree, and argument values are copied directly into the
// buffers in the invocation structure.
//   although a failure to parse the incoming command line is probably a fatal condition
// for most applications, the parsing routine simply logs the error and returns a nonzero
// value to signal that it could not complete the parse.
uint8_t light_cli_process_command_line(struct light_command *root, struct light_cli_invocation *invoke, int argc, char *argv[])
{
        // command line parsing algorithm

        //   first pass classifies all tokens as either option flags or bare strings
        // (where bare strings may be either the name of a command, or an argument
        // [to either an option or command]).
        //   second pass, which relies on the prior construction of a table of all
        // options and commands and the arguments they take, performs the task of
        // identifying commands and options by name, and binding argument values to
        // the commands and options which expect them.
        struct cli_token token[MAX_TOKENS];
        //   CLAMPED, because the classifier loop below stops at MAX_TOKENS while the matcher
        // loop that follows it walked all the way to argc -- so an argv longer than the token
        // array had its tail matched out of uninitialised stack. Dropping the excess is the
        // honest failure: it is reported, and every token that did fit is still parsed
        if(argc > MAX_TOKENS) {
                light_warn("command line has %d tokens, only the first %d will be parsed",
                                argc, MAX_TOKENS);
                argc = MAX_TOKENS;
        }
        // token zero is a special case where we extract the command name from the path
        token[0].type = TOKEN_CMDARG;
        uint8_t *cmd_name = _cli_basename((uint8_t *)argv[0]);
#ifdef _WIN32
        // strip the .exe extension so the derived command name matches registered
        // command names (e.g. "crush.exe" -> "crush"), as it would on POSIX platforms
        // where executables carry no extension
        size_t cmd_name_len = strlen((char *)cmd_name);
        if(cmd_name_len > 4 && strcasecmp((char *)cmd_name + cmd_name_len - 4, ".exe") == 0) {
                cmd_name[cmd_name_len - 4] = '\0';
        }
#endif
        token[0].value = cmd_name;
        for(int i = 1; i < argc && i < MAX_TOKENS; i++) {
                if(argv[i][0] == '-') {
                        if(argv[i][1] == '-') {
                                token[i].type = TOKEN_OPT_L;
                                token[i].value = &argv[i][2];
                        } else {
                                token[i].type = TOKEN_OPT_S;
                                token[i].value = &argv[i][1];
                        }
                } else {
                        token[i].type = TOKEN_CMDARG;
                        token[i].value = argv[i];
                }
        }
#define STATE_MATCH 0
#define STATE_BIND 1
        //   context nesting follows a simple protocol: there is always exactly one
        // context command set, and zero or one context options set. if it is set,
        // the context option always takes precedence over the context command.
        //   the variable 'to_bind' is a counter which tracks the (max) number of
        // remaining arguments which the matched command may bind. this counter is
        // decremented every time an argument is bound (to the command, not an option).
        struct light_command *context = root;
        struct light_cli_option_value *context_opt = NULL;
        uint8_t to_bind;
        uint8_t state = STATE_MATCH;
        for(uint8_t i = 0; i < argc; i++) {
                switch(token[i].type) {
                case TOKEN_CMDARG:
                // note that these state-dependent structures are not mutually exclusive-
                // if state changes during the first block, both statements may execute
                        if(state == STATE_MATCH) {
                                struct light_command *next;
                                // determine if this string matches a subcommand...
                                if(next = light_cli_find_command(context, token[i].value)) {
                                        context = next;
                                        invoke->target = context;
                                } else { // ...or if it's time to start binding arguments
                                        invoke->target = context;
                                        to_bind = context->arg_max;
                                        state = STATE_BIND;
                                }
                        }
                        if(state == STATE_BIND) {
                                if(context_opt) {
                                        context_opt->value = token[i].value;
                                        context_opt = NULL;
                                } else {
                                        if(invoke->args_bound >= LIGHT_CLI_MAX_ARGS) {
                                                light_error("too many arguments to command '%s' (max: %d)", light_cli_command_get_full_name(invoke->target), invoke->target->arg_max);
                                                return LIGHT_INVALID;
                                        }
                                        if(to_bind <= 0)
                                                light_warn("excess arguments supplied to command '%s'", light_cli_command_get_full_name(invoke->target));
                                        invoke->arg[invoke->args_bound++] = token[i].value;
                                        to_bind--;
                                }
                        }
                        break;
                case TOKEN_OPT_S:
                case TOKEN_OPT_L:
                        if(state == STATE_MATCH) {
                                invoke->target = context;
                                //   to_bind is initialised on EVERY transition into STATE_BIND,
                                // not only the one in the CMDARG case above. This transition
                                // left it holding stack garbage, so a command whose first
                                // bindable token was an option -- `font add --local-file <path>`
                                // -- counted its positionals against an arbitrary limit: a
                                // spurious "excess arguments" warning whenever the garbage
                                // happened to be <= 0, which is why the same line warned from
                                // `crush console` but not from a shell
                                to_bind = context->arg_max;
                                state = STATE_BIND;
                        }
                        struct light_cli_option_value *optval = &invoke->option[invoke->option_count++];
                        uint8_t *eq_idx = strchr(token[i].value, '=');
                        // option strings containing a '=' character have a value embedded
                        if(eq_idx) {
                                char optname[LIGHT_CLI_OPTION_VALUE_MAX];
                                strncpy(optname, token[i].value, (eq_idx - token[i].value));
                                struct light_cli_option *option = light_cli_find_command_option(context, optname);

                                if(!option) {
                                        light_error("no option named '%s' exists for command '%s'",
                                                        optname, light_cli_command_get_short_name(context));
                                        return LIGHT_INVALID;
                                }
                                optval->option = option;
                                optval->value = ++eq_idx;
                        } else {
                                struct light_cli_option *option = light_cli_find_command_option(context, token[i].value);

                                if(!option) {
                                        light_error("no option named '%s' exists for command '%s'",
                                                token[i].value, light_cli_command_get_short_name(context));
                                        return LIGHT_INVALID;
                                }
                                switch(option->type) {
                                case LIGHT_CLI_OPTION:
                                        // set this option as the bind context, so its argument is bound
                                        context_opt = optval;
                                        optval->value = token[i].value;
                                        break;
                                case LIGHT_CLI_SWITCH:
                                        optval->value = "1";
                                }
                                optval->option = option;
                        }
                        break;
                }
        }
        uint8_t ref_depth = 0;
        const uint8_t *full_name = light_cli_command_get_full_name(invoke->target);
        light_debug("finished parsing command line, target command: '%s'", full_name);
        return LIGHT_OK;
}
//   the periodic task the framework calls once every scheduler tick. Does AT MOST ONE unit of
// work per call and always returns promptly, which is what makes it safe to run alongside every
// other periodic task (including, on a single-core target, the one draining log output) rather
// than the one-shot task it used to be -- a one-shot task runs to completion before periodic
// tasks are even scheduled, so a command that blocks (a console reading from a human) used to
// block them from ever starting.
//   the launch-time command line is still dispatched exactly once, on this task's first tick;
// after that, each tick drains and dispatches at most one line from the queue
// light_cli_queue_line() feeds, so a session queueing many lines cannot starve anything else by
// having them all run back-to-back inside a single tick.
//
//   THIS TASK MUST BE SCHEDULED AHEAD OF WHATEVER FEEDS THE QUEUE, and that is load-bearing
// rather than incidental. A feeder reads one line per tick and this drains one per tick, so the
// queue holds at most a single line, dispatched on the tick AFTER it was read. When the feeder
// ends the session -- an interactive console reaching EOF -- it does so by returning
// LF_STATUS_SHUTDOWN, and the scheduler abandons the rest of that pass the moment it sees one.
// So the last line survives only because this task already ran earlier in the same pass. Were
// the order reversed, every session would silently lose whatever was typed last.
//   It holds because periodic tasks run in registration order and this one is registered at
// light_cli's MODULE_LOAD, while a feeder belongs to an application module -- which loads after
// its dependencies -- or is registered later still by a command handler. Nothing enforces it
// beyond that, which is why it is written down here.
//   font-crusher's cmd_console__interactive_runs_every_queued_line pins the consequence: it
// counts the commands that actually ran, and a lost last line shows up as a count one short.
uint8_t cli_task(struct light_application *app)
{
        static bool static_invoke_dispatched = false;

        if(!static_invoke_dispatched) {
                static_invoke_dispatched = true;
                if(static_invoke.target)
                        return light_cli_dispatch_command_line(&static_invoke);
        }

        struct light_message queued;
        if(!light_stream_mqueue_try_get(&queue_lock, &queue, &queued))
                return LF_STATUS_RUN;
        light_cli_run_line(queue_root, queued.text);
        return LF_STATUS_RUN;
}
//   calls the handler for a parsed invocation, following any alias chain it returns.
//
//   SEPARATE FROM cli_task() because it has to be callable more than once per process: the
// launch-time path dispatches the single static_invoke and is done, while a console dispatches a
// freshly-parsed invocation per line typed. The task is now nothing but that one call, so both
// paths follow aliases and report failures identically -- which they would not for long if the
// console grew its own copy of this loop
uint8_t light_cli_dispatch_command_line(struct light_cli_invocation *invoke)
{
        //   no target means nothing to run, and is not an error. static_invoke is file-scope, so
        // target is NULL until a command line is parsed into it -- which happens for an
        // application that was given no arguments and has no baked boot command, and for one
        // whose command line failed to parse. That parse failure used to be light_fatal(), so
        // this could not be reached; it is light_error() now, because exit(-1) on a target with
        // no console is an unexplained hang
        if(!invoke->target) {
                light_debug("no command to dispatch");
                return LF_STATUS_RUN;
        }
        //   a command with no handler at all, which every intermediate node in a tree is
        // entitled to be -- there is no reason `crush font` must do something just because
        // `crush font add` does. Calling through the null pointer is what used to happen
        if(!invoke->target->handler) {
                light_error("command '%s' has no handler; it takes a subcommand",
                                light_cli_command_get_short_name(invoke->target));
                return LF_STATUS_ERROR;
        }
        const uint8_t *full_name = light_cli_command_get_full_name(invoke->target);
        light_debug("calling command handler for for command '%s'", full_name);
        struct light_command *last_command = invoke->target;
        struct light_cli_invocation_result result = invoke->target->handler(invoke);
        uint8_t reference_depth = 0;
        while(result.code != LIGHT_CLI_RESULT_SUCCESS) {
                switch (result.code)
                {
                case LIGHT_CLI_RESULT_ALIAS:
                        if(reference_depth >= LIGHT_CLI_MAX_REF_DEPTH) {
                                light_error("command invocation exceeded maximum alias depth of %d", LIGHT_CLI_MAX_REF_DEPTH);
                                return LF_STATUS_ERROR;
                        }
                        const uint8_t *source = light_cli_command_get_full_name(last_command);
                        const uint8_t *target = light_cli_command_get_full_name(result.value.command);
                        light_debug("command '%s' aliased to target command '%s'", source, target);
                        last_command = result.value.command;
                        reference_depth++;
                        //   an alias target with no handler is the same null call the entry
                        // check above catches, reached the long way round
                        if(!last_command->handler) {
                                light_error("alias target '%s' has no handler",
                                                light_cli_command_get_short_name(last_command));
                                return LF_STATUS_ERROR;
                        }
                        result = last_command->handler(invoke);
                        break;
                
                case LIGHT_CLI_RESULT_ERROR:
                        light_error("handler for command '%s' returned ERROR status",
                                light_cli_command_get_short_name(last_command));
                        return LF_STATUS_ERROR;
                }
        }
        light_debug("command handler for '%s' completed successfully",
                light_cli_command_get_short_name(last_command));
        return LF_STATUS_RUN;
}
struct light_command *light_cli_create_command(
                                struct light_command *parent,
                                const uint8_t *name,
                                const uint8_t *description,
                                struct light_cli_invocation_result (*handler)(struct light_cli_invocation *))
{
        if(!parent)
                return light_cli_create_command(&root_command, name, description, handler);
        struct light_command *command;
        if(!(command = light_alloc(sizeof(struct light_command)))) {
                light_warn("could not create new command '%s', failed to allocate memory", name);
                return NULL;
        }

        command->short_name = name;
        command->description = description;
        command->handler = handler;
        light_cli_register_command(parent, command);
        return command;
}
void light_cli_register_command(
                                struct light_command *parent,
                                struct light_command *command)
{        
        if(!parent)
                return light_cli_register_command(&root_command, command);
        if(parent->child_count >= LIGHT_CLI_MAX_SUBCOMMANDS) {
                light_warn("failed to register command '%s', parent command exceeded maximum subcommand count", command->short_name);
                return;
        }
        command->parent = parent;
        parent->child[parent->child_count++] = command;
        // the "full name" string is heap allocated separately to the command object
        command->full_name = cli_command_get_full_name(command);
        light_trace("added subcommand '%s' to command '%s'", command->short_name, parent->short_name);
}
void light_cli_register_option_ctx(
                                struct light_command *command,
                                struct light_cli_option *option)
{
        if(!command)
                return light_cli_register_option_ctx(&root_command, option);
        if(command->option_count >= LIGHT_CLI_MAX_OPTIONS) {
                light_warn("could not add option '%s' to command '%s': max options reached", light_cli_option_get_name(option), light_cli_command_get_short_name(command));
                return;
        }
        command->option[command->option_count++] = option;
        light_trace("added option '%s' to command '%s'", light_cli_option_get_name(option), light_cli_command_get_short_name(command));
}

struct light_command *light_cli_find_command(
                                struct light_command *parent, const uint8_t *name)
{
        if(!parent) {
                return light_cli_find_command(&root_command, name);
        }
        for(uint8_t i = 0; i < parent->child_count && i < LIGHT_CLI_MAX_SUBCOMMANDS; i++) {
                if(strcmp(light_cli_command_get_short_name(parent->child[i]), name) == 0) {
                        return parent->child[i];
                }
        }
        return NULL;
}

struct light_cli_option *light_cli_find_command_option(
                                struct light_command *command, const uint8_t *name)
{
        if(!command) {
                return light_cli_find_command_option(&root_command, name);
        }
        for(uint8_t i = 0; i < command->option_count && i < LIGHT_CLI_MAX_OPTIONS; i++) {
                if(!strncmp(light_cli_option_get_name(command->option[i]), name, LIGHT_OBJ_NAME_LENGTH)) {
                        return command->option[i];
                }
        }
        //   THEN BY SINGLE-LETTER CODE, in a second pass. Every option carries a `code` and
        // every application supplies one, but nothing ever compared against it -- lookup was by
        // name only, so '-t' for '--font' silently reported "no option named 't'" and the short
        // form of every option in every one of these projects did not work.
        //
        //   a SECOND pass rather than a second test inside the loop above, so that an option
        // whose name really is one character always beats an earlier option that happens to
        // carry that letter as its code. Names are what the user wrote out in full; a code is
        // an abbreviation, and an abbreviation should never shadow the full spelling
        if(name[0] && !name[1]) {
                for(uint8_t i = 0; i < command->option_count && i < LIGHT_CLI_MAX_OPTIONS; i++) {
                        if(light_cli_option_get_code(command->option[i]) == (const char)name[0]) {
                                return command->option[i];
                        }
                }
        }
        return NULL;
}

const uint8_t *light_cli_invocation_get_arg_value(struct light_cli_invocation *invoke, const uint8_t index)
{
        if(index >= invoke->args_bound) return NULL;
        return invoke->arg[index];
}
const uint8_t *light_cli_invocation_get_option_value(struct light_cli_invocation *invoke, const uint8_t *option_name)
{
        struct light_cli_option *option = light_cli_find_command_option(invoke->target, option_name);
        if(!option) return NULL;
        for(uint8_t i = 0; i < invoke->option_count; i++) {
                if(invoke->option[i].option == option) {
                        return invoke->option[i].value;
                }
        }
        return NULL;
}
