/*
 *  light_cli/src/line.c
 *  running a command line that arrives as a string at runtime, and describing a command tree
 *
 *  authored by Alex Fulton
 *  created august 2026
 *
 *  The launch-time path in module.c and command.c handles exactly one command line per process,
 *  handed to it as an argv the C runtime already tokenized. This file is what a console needs
 *  instead: a whole command line in one writable string, tokenized here, parsed into a fresh
 *  invocation and dispatched -- repeatedly, for as long as lines keep arriving.
 *
 *  No stdio and no platform calls. Reading the lines is the application's job (see
 *  font-crusher's `crush console`), because only the application knows whether they come from a
 *  terminal, a file or a UART, and that choice is the whole of what is not portable here.
 */
#include <light.h>
#include <light_cli.h>
#include <stdio.h>
#include <string.h>

#include "cli_private.h"

#define QUOTE_NONE                      0

// how many argument placeholders a usage line shows before giving up and printing an ellipsis.
// arg_max is 16 for a command that wants everything, and a usage line listing sixteen <arg>
// placeholders describes nothing
#define HELP_ARGS_SHOWN                 4
// column the option descriptions are padded out to, so a list of them reads as a table
#define HELP_OPTION_NAME_WIDTH          14

uint8_t light_cli_tokenize_line(uint8_t *line, char *argv[], uint8_t argv_max, uint8_t *argc_out)
{
        if(!line || !argv || !argc_out || !argv_max)
                return LIGHT_INVALID;

        uint8_t argc = 0;
        uint8_t *read = line;
        // written first, so a caller that ignores the return value still sees a count rather
        // than whatever was on its stack
        *argc_out = 0;

        while(*read) {
                while(*read == ' ' || *read == '\t')
                        read++;
                //   END OF THE LINE, or a comment that runs to the end of it. '#' is only a
                // comment where a token would have started: inside a word or inside quotes it
                // is an ordinary character, which is what a fragment in a path or a hex colour
                // literal needs it to be
                if(!*read || *read == '#')
                        break;
                if(argc >= argv_max) {
                        light_error("command line has more than %d tokens", argv_max);
                        return LIGHT_INVALID;
                }
                //   COMPACTED IN PLACE. `write` trails `read` by however many quote characters
                // have been removed so far, so no allocation and no second buffer is involved
                // -- and this is why `line` has to be writable and has to outlive the argv
                uint8_t *write = read;
                uint8_t quote = QUOTE_NONE;
                argv[argc++] = (char *)write;

                while(*read) {
                        if(quote == QUOTE_NONE && (*read == ' ' || *read == '\t'))
                                break;
                        if(*read == '"' || *read == '\'') {
                                //   a quote character opens a group, or closes the group it
                                // opened. The OTHER quote character inside a group is literal,
                                // so a path in double quotes may contain an apostrophe
                                if(quote == QUOTE_NONE) {
                                        quote = *read++;
                                        continue;
                                }
                                if(quote == *read) {
                                        quote = QUOTE_NONE;
                                        read++;
                                        continue;
                                }
                        }
                        // NO BACKSLASH ESCAPE. See the header: on Windows that character is the
                        // path separator, and eating it would break every path typed at a prompt
                        *write++ = *read++;
                }
                if(quote != QUOTE_NONE) {
                        light_error("unterminated %c quote in command line", quote);
                        return LIGHT_INVALID;
                }
                //   step over the delimiter BEFORE terminating the token. For a token that
                // contained no quotes `write` has caught up with `read`, so the terminator goes
                // exactly where that delimiter is -- and `read` must already be past it
                if(*read)
                        read++;
                *write = '\0';
        }
        *argc_out = argc;
        return LIGHT_OK;
}
uint8_t light_cli_run_line(struct light_command *root, uint8_t *line)
{
        char *argv[LIGHT_CLI_MAX_TOKENS];
        uint8_t argc = 0;

        if(!line)
                return LIGHT_INVALID;
        const uint8_t *root_name = root ? light_cli_command_get_short_name(root) : NULL;
        if(!root_name) {
                //   root_command itself is anonymous: it is a placeholder that owns the real
                // root commands and has no name to synthesise an argv[0] from. An application
                // passes its own root command here, the one whose name matches its executable
                light_error("cannot run a command line against an unnamed root command");
                return LIGHT_INVALID;
        }
        //   argv[0] IS THE ROOT COMMAND'S NAME, not a program path to be discarded.
        // light_cli_process_command_line() matches its first token against the children of the
        // parse root exactly as it matches argv[0]'s basename at launch, so "font list" typed
        // at a prompt has to be parsed as though a shell had run "crush font list"
        argv[0] = (char *)root_name;
        if(light_cli_tokenize_line(line, &argv[1], LIGHT_CLI_MAX_TOKENS - 1, &argc))
                return LIGHT_INVALID;
        //   a blank line, or one that was nothing but a comment. Not an error: a script gets to
        // be spaced and commented like any other source file
        if(!argc)
                return LIGHT_OK;

        //   TOLERATE THE PROGRAM NAME BEING TYPED ANYWAY. "crush font list" is what the
        // documentation says and what a shell history holds, so it is what gets pasted at a
        // prompt -- and without this it parses as the root command with a stray argument that
        // happens to spell its own name. Rather than shifting the array, the whole thing slides
        // down onto the typed name, which is already the correct spelling of argv[0]
        char **run_argv = argv;
        int run_argc = argc + 1;
        if(!strcmp(argv[1], (const char *)root_name)) {
                run_argv = &argv[1];
                run_argc = argc;
        }
        //   the parse starts at the root's PARENT, because that is the level whose children
        // token 0 is matched against. For an application's root command that is root_command;
        // for a deeper command it is that command's owner, which is what makes it possible to
        // interpret lines relative to a subcommand
        struct light_command *parse_root = root->parent ? root->parent : &root_command;
        //   A FRESH INVOCATION PER LINE, zeroed. The parser appends to option[] and arg[] and
        // trusts the counts it finds there, so reusing one would accumulate the previous line's
        // arguments on top of this line's -- which is precisely why the launch-time path can get
        // away with a single file-scope static_invoke: it is parsed into exactly once
        struct light_cli_invocation invoke = {0};
        if(light_cli_process_command_line(parse_root, &invoke, run_argc, run_argv))
                return LIGHT_INVALID;
        if(light_cli_dispatch_command_line(&invoke) == LF_STATUS_ERROR)
                return LIGHT_INVALID;
        return LIGHT_OK;
}
void light_cli_print_command_help(struct light_command *command)
{
        if(!command)
                command = &root_command;
        struct light_stream *out = light_stream_stdout;
        //   full_name is built at registration and is NULL for root_command, which is never
        // registered; short_name is NULL for it too. A tree walked from an application's own
        // root never lands here, but a caller handed the placeholder deserves a line rather
        // than a fault
        const uint8_t *name = light_cli_command_get_full_name(command);
        if(!name)
                name = light_cli_command_get_short_name(command);

        //   the argument placeholders: required ones in angle brackets, optional ones also in
        // square brackets. There are no argument NAMES to print -- a command declares only how
        // many it takes -- so this says how many and which are optional, and the description
        // below is where a command explains what they are
        char args[64] = "";
        size_t used = 0;
        for(uint8_t i = 0; i < command->arg_max && i < HELP_ARGS_SHOWN; i++) {
                int written = snprintf(args + used, sizeof(args) - used, "%s",
                                (i < command->arg_min) ? " <arg>" : " [<arg>]");
                if(written < 0 || (size_t)written >= sizeof(args) - used)
                        break;
                used += (size_t)written;
        }
        if(command->arg_max > HELP_ARGS_SHOWN)
                snprintf(args + used, sizeof(args) - used, " ...");

        light_stream_message_f_faster(out, "usage: %s%s%s%s\n",
                        name ? name : (const uint8_t *)"(root)",
                        command->child_count ? " <subcommand>" : "",
                        command->option_count ? " [options]" : "",
                        args);
        if(command->description)
                light_stream_message_f_faster(out, "  %s\n", command->description);

        if(command->child_count) {
                light_stream_message_f_faster(out, "\nsubcommands:\n");
                for(uint8_t i = 0; i < command->child_count && i < LIGHT_CLI_MAX_SUBCOMMANDS; i++) {
                        struct light_command *child = command->child[i];
                        light_stream_message_f_faster(out, "  %-*s  %s\n",
                                        HELP_OPTION_NAME_WIDTH,
                                        light_cli_command_get_short_name(child),
                                        light_cli_command_get_description(child) ?
                                                light_cli_command_get_description(child) :
                                                (const uint8_t *)"");
                }
        }
        if(command->option_count) {
                light_stream_message_f_faster(out, "\noptions:\n");
                for(uint8_t i = 0; i < command->option_count && i < LIGHT_CLI_MAX_OPTIONS; i++) {
                        struct light_cli_option *option = command->option[i];
                        //   an OPTION takes a value and a SWITCH does not, and that difference
                        // is the one thing a reader cannot guess from the name
                        light_stream_message_f_faster(out, "  -%c, --%-*s%s  %s\n",
                                        light_cli_option_get_code(option),
                                        HELP_OPTION_NAME_WIDTH,
                                        light_cli_option_get_name(option),
                                        light_cli_option_get_type(option) == LIGHT_CLI_OPTION ?
                                                "<value>" : "       ",
                                        light_cli_option_get_description(option) ?
                                                light_cli_option_get_description(option) :
                                                (const uint8_t *)"");
                }
        }
}
