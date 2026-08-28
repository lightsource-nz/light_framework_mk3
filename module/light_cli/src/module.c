/*
 *  light_cli/src/module.c
 *  functionality for command-line applications on the light framework
 * 
 *  authored by Alex Fulton
 *  created october 2024
 * 
 */

#include <module/mod_light_cli.h>
#include <light_cli.h>
#include <string.h>

#include "cli_private.h"

static void light_command_event(const struct light_module *mod, uint8_t event_id, void *arg);
static void handle_command_line(int argc, char *argv[]);

Light_Module_Define(light_cli, light_command_event, &light_core);

static void light_command_event(const struct light_module *mod, uint8_t event_id, void *arg)
{
        switch (event_id)
        {
        case LF_EVENT_MODULE_LOAD:
                // periodic, not one-shot: see cli_task()'s own comment for why
                light_module_register_periodic_task(mod, "light_cli_task", cli_task);
                light_cli_init();
                break;
        case LF_EVENT_APP_LAUNCH:
                //   every static object has been loaded by now, so anything still waiting for
                // an application root command is never going to get one. Reported before the
                // command line is parsed, so the explanation precedes the failure it causes
                light_core__resolve_app_root();
                struct light_event_app_launch *event = (struct light_event_app_launch *)arg;
                handle_command_line(event->argc, event->argv);
                break;
        
        default:
                break;
        }
        
}
#ifdef LIGHT_BOOT_COMMAND
// generous enough for any realistic baked command; the parser's own limit is MAX_TOKENS (64)
#define LIGHT_CLI_BOOT_ARGS_MAX         16

//   the baked command line, split in place into an argv-shaped array.
//
//   NOT const, and NOT on the stack. light_cli_process_command_line() stores pointers INTO argv
// for every argument and option value rather than copying them, so this buffer has to outlive
// the parse and stay reachable for as long as the invocation is dispatched -- and the splitter
// writes NULs into it to terminate each token
static uint8_t boot_command[] = LIGHT_BOOT_COMMAND;
static char *boot_argv[LIGHT_CLI_BOOT_ARGS_MAX];

//   splits on spaces and tabs only. No quoting and no escapes: this is a build-time constant
// written by whoever configured the firmware, not untrusted input, and a command needing quoted
// arguments is a sign the knob is being asked to do too much.
//   TOKENS START AT boot_argv[1], leaving slot 0 for the root command name the caller supplies.
// Returns how many tokens the string held, not the resulting argc
static int _split_boot_command(void)
{
        int count = 0;
        uint8_t *cursor = boot_command;

        while(*cursor && count + 1 < LIGHT_CLI_BOOT_ARGS_MAX) {
                while(*cursor == ' ' || *cursor == '\t')
                        cursor++;
                if(!*cursor)
                        break;
                boot_argv[1 + count++] = (char *)cursor;
                while(*cursor && *cursor != ' ' && *cursor != '\t')
                        cursor++;
                if(*cursor)
                        *cursor++ = '\0';
        }
        return count;
}
#endif

static void handle_command_line(int argc, char *argv[])
{
#ifdef LIGHT_BOOT_COMMAND
        //   a target has no console to type a command at and passes (0, NULL) to
        // light_framework_run(), so fall back to the command baked in at build time. A real
        // command line always wins: the baked one is a default, not an override, so a host
        // build configured with LIGHT_BOOT_COMMAND still behaves normally.
        //
        //   THE ROOT COMMAND NAME IS SUPPLIED, NOT WRITTEN OUT. process_command_line() takes
        // argv[0] as the root command, so the baked string used to have to lead with it -- which
        // meant the string encoded the application's name, and one string could not serve two
        // applications. light_ui's four board demos are exactly that case: separate applications,
        // one shared preset, one baked command.
        //   so slot 0 is filled in from the root command itself and the string carries only what
        // to run: "backlight 600". Writing the name anyway is still accepted, the same tolerance
        // light_cli_run_line() extends to a line typed at a console, so an existing baked string
        // keeps working.
        if(argc < 1 || !argv) {
                struct light_command *root = light_command_app_root();
                const uint8_t *root_name = root ? light_command_get_short_name(root) : NULL;
                int tokens = _split_boot_command();

                if(!root_name) {
                        light_error("cannot run the baked boot command: this application has no "
                                        "root command to run it against");
                        return;
                }
                boot_argv[0] = (char *)root_name;
                argv = boot_argv;
                argc = tokens + 1;
                if(tokens && !strcmp(boot_argv[1], (const char *)root_name)) {
                        argv = &boot_argv[1];
                        argc = tokens;
                }
                light_info("running baked boot command '%s'", LIGHT_BOOT_COMMAND);
        }
#endif
        //   nothing to do rather than something to complain about: an application with no
        // arguments and no baked command simply has no command to run, and cli_task() handles
        // the resulting empty invocation
        if(argc < 1 || !argv)
                return;

        if(light_cli_process_command_line(&root_command, &static_invoke, argc, argv)) {
                //   an error, not fatal. light_fatal() is exit(-1), which on a target with no
                // console is an unexplained hang -- a command line that will not parse should
                // cost the feature it configures, not the boot. cli_task() finds no target and
                // dispatches nothing
                light_error("could not process the incoming command line");
        }
}
