#include <light.h>
#include <module/mod_light_cli.h>
#include <light_cli.h>

#define CMD_ROOT        NULL

static void demo_cli_app_event(const struct light_module *mod, uint8_t event, void *arg);
static uint8_t demo_cli_main(struct light_application *app);
static struct light_cli_invocation_result demo_do_cmd(struct light_cli_invocation *invoke);
Light_Command_Define(demo_command, CMD_ROOT, "demo_command", "a simple command to demonstrate the light_cli interface",
        demo_do_cmd, 0, 2);
// three distinctly-named options registered on the same command -- this shape (2+ options,
// only some of which are actually passed on a given invocation) is what's needed to exercise
// light_cli_find_command_option(): a lookup for any one option's value has to correctly skip
// over the *other* registered options rather than stopping at the first one that doesn't
// match by name
Light_Command_Option_Define(opt_demo_alpha, &demo_command, "alpha", 'a', "first demo option");
Light_Command_Option_Define(opt_demo_beta, &demo_command, "beta", 'b', "second demo option");
Light_Command_Option_Define(opt_demo_gamma, &demo_command, "gamma", 'g', "third demo option, deliberately left unset by the regression test");

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
        const uint8_t *alpha = light_cli_invocation_get_option_value(invoke, "alpha");
        const uint8_t *beta = light_cli_invocation_get_option_value(invoke, "beta");
        // 'gamma' is intentionally never passed on the command line by the regression test --
        // asserting it comes back unset is what catches light_cli_find_command_option()
        // resolving to the wrong (but registered and bound) sibling option instead of correctly
        // reporting "not found"
        bool gamma_set = light_cli_invocation_option_is_set(invoke, "gamma");
        light_info("option values: alpha='%s' beta='%s' gamma_set=%s",
                alpha ? alpha : (const uint8_t *)"(null)",
                beta ? beta : (const uint8_t *)"(null)",
                gamma_set ? "true" : "false");

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
