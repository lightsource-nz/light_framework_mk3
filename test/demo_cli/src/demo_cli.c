#include <light.h>
#include <module/mod_light_cli.h>
#include <light_cli.h>

#define CMD_ROOT        NULL

static void demo_cli_app_event(const struct light_module *mod, uint8_t event, void *arg);
static uint8_t demo_cli_main(struct light_application *app);
static struct light_cli_invocation_result demo_do_cmd(struct light_cli_invocation *invoke);
Light_Command_Define(demo_command, CMD_ROOT, "demo_command", "a simple command to demonstrate the light_cli interface",
        demo_do_cmd, 0, 2);

Light_Application_Define(demo_cli, demo_cli_app_event, demo_cli_main,
                                                                &light_cli,
                                                                &light_core);

void main(int argc, char *argv[])
{
        light_framework_init();
        light_framework_run(argc, argv);
}

static struct light_cli_invocation_result demo_do_cmd(struct light_cli_invocation *invoke)
{
        light_info("writing to log from inside command handler: alpha=%s, beta=%s");

        return (struct light_cli_invocation_result) { .code = LIGHT_CLI_RESULT_SUCCESS};
}

static void demo_cli_app_event(const struct light_module *mod, uint8_t event, void *arg)
{
        switch (event) {
        case LF_EVENT_APP_LAUNCH:
                light_info("demo app received LAUNCH event","");
                break;
        case LF_EVENT_APP_SHUTDOWN:
                light_info("demo app received SHUTDOWN event","");
                break;
        }
}

static uint8_t demo_cli_main(struct light_application *app)
{
        return LF_STATUS_SHUTDOWN;
}
