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

#include "cli_private.h"

static void light_command_event(const struct light_module *mod, uint8_t event_id, void *arg);
static void handle_command_line(int argc, char *argv[]);

Light_Module_Define(light_cli, light_command_event, &light_core);

static void light_command_event(const struct light_module *mod, uint8_t event_id, void *arg)
{
        switch (event_id)
        {
        case LF_EVENT_MODULE_LOAD:
                light_module_register_one_shot_task(mod, "light_cli_task", cli_task);
                light_cli_init();
                break;
        case LF_EVENT_APP_LAUNCH:
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
// arguments is a sign the knob is being asked to do too much
static int _split_boot_command(void)
{
        int argc = 0;
        uint8_t *cursor = boot_command;

        while(*cursor && argc < LIGHT_CLI_BOOT_ARGS_MAX) {
                while(*cursor == ' ' || *cursor == '\t')
                        cursor++;
                if(!*cursor)
                        break;
                boot_argv[argc++] = (char *)cursor;
                while(*cursor && *cursor != ' ' && *cursor != '\t')
                        cursor++;
                if(*cursor)
                        *cursor++ = '\0';
        }
        return argc;
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
        //   the baked string leads with the application's own root command name, exactly as a
        // shell command line does -- process_command_line() takes argv[0] as the root command
        // (via _cli_basename), not as a program path to discard
        if(argc < 1 || !argv) {
                argc = _split_boot_command();
                argv = boot_argv;
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
