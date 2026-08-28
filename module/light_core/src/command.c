/*
 *  light_core/src/command.c
 *  the command TREE: registration, lookup, and resolving the application root command
 *
 *  MOVED HERE FROM light_cli/src/command.c -- see light_command.h for why. In short:
 *  Light_Application_Define() declares an application's root command, so the macros and the
 *  registration behind them have to live below light_cli rather than inside it.
 *
 *  What stayed in light_cli is everything that processes a command LINE: the tokenizer, the
 *  parser, dispatch, the deferred-line queue and cli_task(). This file never looks at an argv.
 *
 *  light_command_print_help() came along too, though it reads like presentation rather than
 *  structure: it walks the tree and nothing else, and the default handler for an auto-declared
 *  root command is exactly "print my own subcommands" -- so leaving it behind would have made
 *  light_core depend on light_cli for that one call.
 */
#include <light.h>
#include <stdio.h>
#include <string.h>

// how many argument placeholders a usage line shows before giving up and printing an ellipsis.
// arg_max is 16 for a command that wants everything, and a usage line listing sixteen <arg>
// placeholders describes nothing
#define HELP_ARGS_SHOWN                 4
// column the option descriptions are padded out to, so a list of them reads as a table
#define HELP_OPTION_NAME_WIDTH          14

#define TYPE_NAME_COMMAND               "light_cli:command"

static void command_release(struct light_object *cmd);
struct lobj_type ltype_light_command = {
        .id = TYPE_NAME_COMMAND,
        .release = command_release
};

//   we use this structure as a placeholder to store actual root-level commands,
// but it has no name and cannot be invoked
struct light_command root_command;

//   THE APPLICATION'S ROOT COMMAND and the commands waiting to be attached to it.
//
//   the two arrive through the same linker section in an order nothing controls, so a module's
// LIGHT_COMMAND_APP_ROOT command can perfectly well be loaded before the application's own root is.
// Resolving on the spot would then attach it to NULL, which light_command_register() reads
// as the global root_command -- where the parser would never look for it. Silently, and
// depending on link order. So a command that cannot be resolved yet is parked here and attached
// the moment the root shows up.
//   sized by LIGHT_COMMAND_MAX_SUBCOMMANDS because that is the ceiling on what a single parent can
// hold anyway: a pending list longer than that describes commands that could not be registered
// even if they were resolved.
struct light_command *light_core_app_root_command;
static struct light_command *app_root_candidate;
static struct light_command *pending_app_root[LIGHT_COMMAND_MAX_PENDING];
static uint8_t pending_app_root_count;

//   CAN THIS COMMAND BE REGISTERED YET? Registration computes a full name by walking up the
// parent chain (cli_command_get_full_name()), so EVERY ancestor has to be resolved, not just the
// immediate parent -- and the sentinel is not a pointer that may be followed.
//   this bit is not defensive. The records arrive in link order, so `ui focus` (parent `ui`)
// can perfectly well load before `ui` itself has had its sentinel replaced, and the name walk
// then dereferences address 1. On an RP2350 that is a hard fault during static object loading:
// core 0 dies silently while core 1 keeps echoing the console, which looks like a hang with no
// message rather than a crash.
static bool _ancestry_resolved(struct light_command *command)
{
        struct light_command *next = command;
        for(uint8_t depth = 0; next && depth < LIGHT_COMMAND_MAX_DEPTH; depth++) {
                if(next->parent == LIGHT_COMMAND_APP_ROOT) {
                        //   `next == command` is the point of this check, not a detail. Only the
                        // command that HOLDS the sentinel may resolve it; a descendant found to
                        // have a sentinel further up must wait, because its own registration
                        // would walk that chain and dereference address 1 on the way past.
                        // Resolving the holder replaces its parent with a real pointer, and the
                        // descendant becomes registerable on a later pass
                        return next == command && light_core_app_root_command != NULL;
                }
                if(next->parent == &root_command)
                        return true;
                next = next->parent;
        }
        return true;
}
static void _defer_command(struct light_command *command)
{
        if(pending_app_root_count >= LIGHT_COMMAND_MAX_PENDING) {
                light_warn("cannot defer command '%s': more than %d commands are waiting for "
                                "the application root -- raise LIGHT_COMMAND_MAX_PENDING",
                                command->short_name, LIGHT_COMMAND_MAX_PENDING);
                return;
        }
        pending_app_root[pending_app_root_count++] = command;
}
//   registers whatever has become resolvable, repeatedly, because resolving one command can
// unblock its children: attaching `ui` to the application root is what makes `ui focus`
// registerable. Passes stop as soon as one makes no progress, so this is O(n^2) over a list
// bounded at LIGHT_COMMAND_MAX_SUBCOMMANDS and terminates on anything, including a cycle
static void _drain_pending_app_root(void)
{
        bool progress = true;
        while(progress) {
                progress = false;
                for(uint8_t i = 0; i < pending_app_root_count; ) {
                        struct light_command *command = pending_app_root[i];
                        if(!_ancestry_resolved(command)) {
                                i++;
                                continue;
                        }
                        if(command->parent == LIGHT_COMMAND_APP_ROOT)
                                command->parent = light_core_app_root_command;
                        light_command_register(command->parent, command);
                        pending_app_root[i] = pending_app_root[--pending_app_root_count];
                        progress = true;
                }
        }
}

static void command_release(struct light_object *cmd)
{
        light_free(to_command(cmd));
}

#define LIGHT_CLI_COMMAND_NAME_BUFFER_SIZE      128
static const uint8_t *cli_command_get_full_name(struct light_command *command)
{
        static uint8_t buffer[LIGHT_CLI_COMMAND_NAME_BUFFER_SIZE];
        struct light_command *stack[LIGHT_COMMAND_MAX_DEPTH];
        uint8_t depth = 0;

        //   walk up to the root recording the path, BOUNDED by the size of stack[]. The loop
        // had no such bound, so a tree deeper than LIGHT_COMMAND_MAX_DEPTH wrote past the
        // end of it -- and the depth is whatever the application's command hierarchy happens
        // to be, not something this file controls
        for(struct light_command *next = command;
                        next && next != &root_command && depth < LIGHT_COMMAND_MAX_DEPTH;
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
                const uint8_t *part = light_command_get_short_name(stack[i - 1]);
                size_t len = strlen((const char *) part);
                size_t sep = (i > 1) ? 1 : 0;

                // +1 for the terminator, which is never part of the space a bound may use
                if(used + len + sep + 1 > sizeof(buffer)) {
                        light_warn("command name for '%s' truncated at %u characters",
                                        light_command_get_short_name(command), (unsigned) used);
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

void light_core__autoload_command(void *object)
{
        struct light_command *command = (struct light_command *)object;
        //   LIGHT_COMMAND_APP_ROOT means "whatever application I end up linked into", which is the
        // one parent a library module cannot name at compile time. Anything whose ancestry still
        // passes through it is parked until it can be resolved -- see _ancestry_resolved()
        if(!_ancestry_resolved(command)) {
                _defer_command(command);
                return;
        }
        light_command_register(command->parent, command);
}
void light_core__autoload_option(void *object)
{
        struct light_command_option *option = (struct light_command_option *)object;
        light_command_register_option(option->command, option);
}

struct light_cli_invocation_result light_core_app_root_help(struct light_cli_invocation *invoke)
{
        light_command_print_help(light_core_app_root_command);
        return Result_Success;
}
//   REMEMBERED, NOT REGISTERED. Resolution waits until every static object has been loaded --
// see light_core__resolve_app_root() -- because whether this command should be registered
// at all depends on what else registered, and that is not known until they all have.
void light_core__autoload_app_root(void *object)
{
        struct light_command *command = (struct light_command *)object;
        if(app_root_candidate) {
                //   one application per process, so a second root is a build that has linked
                // two of them together. Refused rather than replacing the first, because the
                // commands already attached to it would be orphaned
                light_error("application root command '%s' ignored: '%s' is already the root",
                                light_command_get_short_name(command),
                                light_command_get_short_name(app_root_candidate));
                return;
        }
        app_root_candidate = command;
}
//   reports anything still waiting once every static object has been loaded. Called from
// light_cli's APP_LAUNCH hook, before the command line is parsed, so the diagnostic lands
// before the failure it explains
void light_core__resolve_app_root(void)
{
        //   THE AUTO ROOT YIELDS TO A HAND-WRITTEN ONE, and this is why resolution waits until
        // every static object has loaded rather than happening as the record arrives.
        //
        //   an application written before Light_Application_Define() declared a root still
        // declares its own, and its subcommands name that symbol as their parent -- by address,
        // at compile time. So the hand-written one is the command that MUST be registered; a
        // second, identically named root beside it would make which one the parser finds depend
        // on link order. Adopting the existing one instead keeps such an application working
        // untouched, which matters because this framework is consumed through a moving major
        // tag: an application does not update in lockstep with it.
        if(app_root_candidate) {
                struct light_command *existing =
                        light_command_find(&root_command, app_root_candidate->short_name);
                if(existing) {
                        light_debug("application declares its own root command '%s'; adopting it",
                                        light_command_get_short_name(existing));
                        light_core_app_root_command = existing;
                } else {
                        light_core_app_root_command = app_root_candidate;
                        light_command_register(&root_command, app_root_candidate);
                }
        }
        //   and only now can anything parented to the sentinel be attached
        _drain_pending_app_root();
        for(uint8_t i = 0; i < pending_app_root_count; i++) {
                light_error("command '%s' asked for LIGHT_COMMAND_APP_ROOT but this application "
                                "defines no root command -- include <light_cli.h> before "
                                "Light_Application_Define()",
                                light_command_get_short_name(pending_app_root[i]));
        }
        pending_app_root_count = 0;
}

struct light_command *light_command_create(
                                struct light_command *parent,
                                const uint8_t *name,
                                const uint8_t *description,
                                struct light_cli_invocation_result (*handler)(struct light_cli_invocation *))
{
        if(!parent)
                return light_command_create(&root_command, name, description, handler);
        struct light_command *command;
        if(!(command = light_alloc(sizeof(struct light_command)))) {
                light_warn("could not create new command '%s', failed to allocate memory", name);
                return NULL;
        }
        //   ZEROED, which it was not. Only short_name, description and handler were assigned
        // below, leaving child_count, option_count, arg_min/arg_max, parent, child[], option[],
        // full_name and the object header as whatever the allocator last had there.
        //   child_count is the one that bites: light_command_find() walks child[] up to it
        // on every parse, so a command created this way and then given a subcommand had a
        // garbage child list under a garbage count, and the parser dereferenced it. It reads as
        // an intermittent crash rather than an obvious one, because it only shows when the heap
        // hands back something other than zeros -- the same allocation is silently harmless on
        // a fresh heap, which is why every host test of this passed for as long as it did and
        // then segfaulted five at a time under an allocator that poisons fresh memory
        // (0xbaadf00d, caught in light_command_get_short_name()).
        //   the object header is left zeroed rather than initialised: nothing puts a
        // runtime-created command into the object tree, so there is no refcount to get right
        // here, and a zeroed header is at least deterministic. Static commands get theirs from
        // Light_Command_Static().
        memset(command, 0, sizeof(*command));

        command->short_name = name;
        command->description = description;
        command->handler = handler;
        light_command_register(parent, command);
        return command;
}
void light_command_register(
                                struct light_command *parent,
                                struct light_command *command)
{        
        if(!parent)
                return light_command_register(&root_command, command);
        if(parent->child_count >= LIGHT_COMMAND_MAX_SUBCOMMANDS) {
                light_warn("failed to register command '%s', parent command exceeded maximum subcommand count", command->short_name);
                return;
        }
        command->parent = parent;
        parent->child[parent->child_count++] = command;
        // the "full name" string is heap allocated separately to the command object
        command->full_name = cli_command_get_full_name(command);
        light_trace("added subcommand '%s' to command '%s'", command->short_name, parent->short_name);
}
void light_command_register_option(
                                struct light_command *command,
                                struct light_command_option *option)
{
        if(!command)
                return light_command_register_option(&root_command, option);
        if(command->option_count >= LIGHT_COMMAND_MAX_OPTIONS) {
                light_warn("could not add option '%s' to command '%s': max options reached", light_command_option_get_name(option), light_command_get_short_name(command));
                return;
        }
        command->option[command->option_count++] = option;
        light_trace("added option '%s' to command '%s'", light_command_option_get_name(option), light_command_get_short_name(command));
}

struct light_command *light_command_find(
                                struct light_command *parent, const uint8_t *name)
{
        if(!parent) {
                return light_command_find(&root_command, name);
        }
        for(uint8_t i = 0; i < parent->child_count && i < LIGHT_COMMAND_MAX_SUBCOMMANDS; i++) {
                if(strcmp(light_command_get_short_name(parent->child[i]), name) == 0) {
                        return parent->child[i];
                }
        }
        return NULL;
}

struct light_command_option *light_command_find_option(
                                struct light_command *command, const uint8_t *name)
{
        if(!command) {
                return light_command_find_option(&root_command, name);
        }
        for(uint8_t i = 0; i < command->option_count && i < LIGHT_COMMAND_MAX_OPTIONS; i++) {
                if(!strncmp(light_command_option_get_name(command->option[i]), name, LIGHT_OBJ_NAME_LENGTH)) {
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
                for(uint8_t i = 0; i < command->option_count && i < LIGHT_COMMAND_MAX_OPTIONS; i++) {
                        if(light_command_option_get_code(command->option[i]) == (const char)name[0]) {
                                return command->option[i];
                        }
                }
        }
        return NULL;
}


void light_command_print_help(struct light_command *command)
{
        if(!command)
                command = &root_command;
        struct light_stream *out = light_stream_stdout;
        //   full_name is built at registration and is NULL for root_command, which is never
        // registered; short_name is NULL for it too. A tree walked from an application's own
        // root never lands here, but a caller handed the placeholder deserves a line rather
        // than a fault
        const uint8_t *name = light_command_get_full_name(command);
        if(!name)
                name = light_command_get_short_name(command);

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
                for(uint8_t i = 0; i < command->child_count && i < LIGHT_COMMAND_MAX_SUBCOMMANDS; i++) {
                        struct light_command *child = command->child[i];
                        light_stream_message_f_faster(out, "  %-*s  %s\n",
                                        HELP_OPTION_NAME_WIDTH,
                                        light_command_get_short_name(child),
                                        light_command_get_description(child) ?
                                                light_command_get_description(child) :
                                                (const uint8_t *)"");
                }
        }
        if(command->option_count) {
                light_stream_message_f_faster(out, "\noptions:\n");
                for(uint8_t i = 0; i < command->option_count && i < LIGHT_COMMAND_MAX_OPTIONS; i++) {
                        struct light_command_option *option = command->option[i];
                        //   an OPTION takes a value and a SWITCH does not, and that difference
                        // is the one thing a reader cannot guess from the name
                        light_stream_message_f_faster(out, "  -%c, --%-*s%s  %s\n",
                                        light_command_option_get_code(option),
                                        HELP_OPTION_NAME_WIDTH,
                                        light_command_option_get_name(option),
                                        light_command_option_get_type(option) == LIGHT_COMMAND_OPTION ?
                                                "<value>" : "       ",
                                        light_command_option_get_description(option) ?
                                                light_command_option_get_description(option) :
                                                (const uint8_t *)"");
                }
        }
}