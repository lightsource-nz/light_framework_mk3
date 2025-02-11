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

static void command_release(struct light_object *cmd)
{
        light_free(to_command(cmd));
}
void light_cli_init()
{
        light_cli_message_init();
}
// we define the internal command-line parser's input limit to 64 tokens
#define MAX_TOKENS              64
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
        for(int i = 0; i < argc && i < MAX_TOKENS; i++) {
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
                                        strncpy(context_opt->value, token[i].value, LIGHT_CLI_OPTION_VALUE_MAX);
                                        context_opt = NULL;
                                } else if(to_bind) {
                                        strncpy(invoke->arg[invoke->args_bound++], token[i].value, LIGHT_CLI_OPTION_VALUE_MAX);
                                        to_bind--;
                                } else {
                                        light_error("too many arguments to command '%s' (max: %d)", invoke->target, invoke->target->arg_max);
                                        return LIGHT_INVALID;
                                }
                        }
                        break;
                case TOKEN_OPT_L:
                        if(state == STATE_MATCH) {
                                invoke->target = context;
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
                                                        optname, light_cli_command_get_name(context));
                                        return LIGHT_INVALID;
                                }
                                optval->option = option;
                                strncpy(optval->value, ++eq_idx, LIGHT_CLI_OPTION_VALUE_MAX);
                        } else {
                                struct light_cli_option *option = light_cli_find_command_option(context, token[i].value);

                                if(!option) {
                                        light_error("no option named '%s' exists for command '%s'",
                                                token[i].value, light_cli_command_get_name(context));
                                        return LIGHT_INVALID;
                                }
                                optval->option = option;
                                // set this option as the bind context, so its argument is bound
                                context_opt = optval;
                        }
                        break;
                }
        }
        uint8_t ref_depth = 0;
        light_debug("finished parsing command line, target command: '%s'", invoke->target);
        struct light_command *last_command = invoke->target;
        struct light_cli_invocation_result result = invoke->target->handler(invoke);
        while(result.code != LIGHT_CLI_RESULT_SUCCESS) {
                switch (result.code)
                {
                case LIGHT_CLI_RESULT_ALIAS:
                        light_debug("command '%s' aliased to target command '%s'",
                                light_cli_command_get_name(invoke->target),
                                light_cli_command_get_name(result.value.command));
                                last_command = result.value.command;
                                result = result.value.command->handler(invoke);
                        break;
                
                case LIGHT_CLI_RESULT_ERROR:
                        light_error("handler for command '%s' returned ERROR status",
                                light_cli_command_get_name(last_command));
                        return LIGHT_EXTERNAL;
                }
        }
        light_debug("command handler for '%s' completed successfully");
        return LIGHT_OK;
}
struct light_command *light_cli_create_subcommand(
                                struct light_command *parent,
                                const uint8_t *name,
                                const uint8_t *description,
                                struct light_cli_invocation_result (*handler)(struct light_cli_invocation *))
{
        if(!parent)
                return light_cli_create_subcommand(&root_command, name, description, handler);
        struct light_command *command;
        if(!(command = light_alloc(sizeof(struct light_command)))) {
                light_warn("could not create new command '%s', failed to allocate memory", name);
                return NULL;
        }

        command->name = name;
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
                light_warn("failed to register command '%s', parent command exceeded maximum subcommand count", command->name);
                return;
        }
        parent->child[parent->child_count++] = command;
        light_trace("added subcommand '%s' to command '%s'", command->name);
}
void light_cli_register_option_ctx(
                                struct light_command *command,
                                struct light_cli_option *option)
{
        if(!command)
                return light_cli_register_option_ctx(&root_command, option);
        if(command->option_count >= LIGHT_CLI_MAX_OPTIONS) {
                light_warn("could not add option '%s' to command '%s': max options reached", light_cli_option_get_name(option), light_cli_command_get_name(command));
                return;
        }
        command->option[command->child_count++] = option;
        light_trace("added option '%s' to command '%s'", light_cli_option_get_name(option), light_cli_command_get_name(command));
}

struct light_command *light_cli_find_command(
                                struct light_command *parent, const uint8_t *name)
{
        if(!parent) {
                return light_cli_find_command(&root_command, name);
        }
        for(uint8_t i = 0; i < parent->child_count && i < LIGHT_CLI_MAX_SUBCOMMANDS; i++) {
                if(strncmp(light_cli_command_get_name(parent->child[i]), name, LIGHT_OBJ_NAME_LENGTH)) {
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
                if(strncmp(light_cli_option_get_name(command->option[i]), name, LIGHT_OBJ_NAME_LENGTH)) {
                        return command->option[i];
                }
        }
        return NULL;
}

const uint8_t *light_cli_invocation_get_option_value(struct light_cli_invocation *invoke, const uint8_t *option_name)
{
        for(uint8_t i = 0; i < invoke->option_count; i++) {
                if(invoke->option[i].option == light_cli_find_command_option(invoke->target, option_name)) {
                        return invoke->option[i].value;
                }
        }
}
