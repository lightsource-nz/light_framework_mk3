/*
 *  light_cli/src/module.c
 *  functionality for command-line applications on the light framework
 * 
 *  authored by Alex Fulton
 *  created october 2024
 * 
 */

#include <light_cli.h>

static void light_command_event(const struct light_module *mod, uint8_t event_id, void *arg);
static void light_command_process_command_line(int argc, char *argv[]);

Light_Module_Define(light_command, light_command_event, &light_core);

static void light_command_event(const struct light_module *mod, uint8_t event_id, void *arg)
{
        switch (event_id)
        {
        case LF_EVENT_MODULE_LOAD:
                light_cli_init();
                break;
        case LF_EVENT_APP_LOAD:
                struct light_event_app_load *event = (struct light_event_app_load *)arg;
                light_command_process_command_line(event->argc, event->argv);
                break;
        
        default:
                break;
        }
        
}
static void light_command_process_command_line(int argc, char *argv[])
{
        // command line parsing algorithm

        //   first pass classifies all tokens as either option flags or bare strings
        // (where bare strings may be either the name of a command, or an argument
        // [to either an option or command]).
        //   second pass, which relies on the prior construction of a table of all
        //   options and commands and the arguments they take, performs the task of
        //   identifying commands and options by name, and binding argument values to
        //   the commands and options which expect them.
        for(int i = 0; i < argc; i++) {

        }
}
