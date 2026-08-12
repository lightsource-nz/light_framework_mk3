#include <light.h>
#include <module/mod_light_cli.h>
#include <light_cli.h>

#include <stdio.h>

#define CMD_ROOT        NULL

static void demo_cli_app_event(const struct light_module *mod, uint8_t event, void *arg);
static uint8_t demo_cli_main(struct light_application *app);
static struct light_cli_invocation_result demo_do_cmd(struct light_cli_invocation *invoke);
// the NAME here has to match the executable's, because a command registered with a NULL
// parent becomes a child of root_command, and light_cli_process_command_line() matches its
// first token against argv[0]'s basename. this said "demo_command" while the binary is
// demo_cli, so argv[0] matched nothing, the target stayed at the unnamed root, and every
// option lookup then failed against a command that has no options -- which is why this test
// had never once passed. crush does the same thing correctly: its root command is "crush"
// and its binary is crush.exe
Light_Command_Define(demo_command, CMD_ROOT, "demo_cli", "a simple command to demonstrate the light_cli interface",
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
        // printf, NOT light_info: this line is the program's RESULT, and the regression test
        // matches on it. every log macro compiles to nothing under RUN_MODE=PRODUCTION
        // (FILTER_LOG_LEVEL DISABLE), so asserting on a logged line made the test pass or fail
        // according to the build mode of whatever project happened to include this one --
        // green in font-crusher's DEBUG build, red in screen-test's PRODUCTION one, for
        // reasons having nothing to do with the CLI behaviour under test.
        // fflush because the framework's own output goes through a background queue, and a
        // test that greps stdout should not depend on the interleaving
        printf("option values: alpha='%s' beta='%s' gamma_set=%s\n",
                alpha ? (const char *)alpha : "(null)",
                beta ? (const char *)beta : "(null)",
                gamma_set ? "true" : "false");
        fflush(stdout);

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
