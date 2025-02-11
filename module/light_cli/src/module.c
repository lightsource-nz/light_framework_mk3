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
static uint8_t cli_task(struct light_application *app);

Light_Module_Define(light_cli, light_command_event, &light_core);

static void light_command_event(const struct light_module *mod, uint8_t event_id, void *arg)
{
        switch (event_id)
        {
        case LF_EVENT_MODULE_LOAD:
                light_module_register_periodic_task(mod, "light_cli_task", cli_task);
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
static void handle_command_line(int argc, char *argv[])
{
        struct light_cli_invocation invoke;
        if(light_cli_process_command_line(&root_command, &static_invoke, argc, argv)) {
                light_fatal("something went wrong while trying to process the incoming command line");
        }
}
static uint8_t cli_task(struct light_application *app)
{
        light_debug("calling command handler for for command '%s'", light_cli_command_get_name(static_invoke.target));
        static_invoke.target->handler(&static_invoke);
}
