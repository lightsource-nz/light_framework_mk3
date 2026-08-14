/*
 *  test_light_object.c
 *  reference counting and naming in the light object model
 *
 *  HOST ONLY: these run the real light_core, linked as the library rather than stubbed, and
 *  inspect what it did. Everything here is single-threaded on purpose -- the refcount
 *  primitives are compare-exchange loops whose CONCURRENT behaviour is a separate question
 *  these tests deliberately do not claim to answer.
 *
 *  These pin down what the implementation currently does, which is not always what a reader
 *  would assume. Where the two differ the test says so in a comment rather than quietly
 *  encoding a surprise as though it were the intent.
 */
#include <light.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//   light_core references `this_app`, so anything linking it has to define an application.
// It is never started: main() below calls the test functions directly rather than going
// through light_framework_run(), so no module load or task scheduling happens
static void _test_app_event(const struct light_module *module, uint8_t event, void *arg) {}
static uint8_t _test_app_main(struct light_application *app) { return LF_STATUS_RUN; }
Light_Application_Define(test_light_object, _test_app_event, _test_app_main, &light_core);

static int failures;

#define CHECK(cond, ...) do { \
        if(!(cond)) { \
                failures++; \
                printf("  FAIL %s:%d: ", __func__, __LINE__); \
                printf(__VA_ARGS__); \
                printf("\n"); \
        } \
} while(0)

// a type with no callbacks: these tests are about the refcount itself, and a release hook
// firing would confuse "what did put() do" with "what did the type do"
static struct lobj_type _type_plain = { .id = "test:plain" };

// counts callback invocations, so the add/del event contract can be checked rather than assumed
static int _child_add_calls;
static int _child_remove_calls;
static int _add_calls;
static void _on_child_add(struct light_object *parent, struct light_object *child) { _child_add_calls++; }
static void _on_child_remove(struct light_object *parent, struct light_object *child) { _child_remove_calls++; }
static void _on_add(struct light_object *obj, struct light_object *parent) { _add_calls++; }
static struct lobj_type _type_events = {
        .id = "test:events",
        .evt_add = _on_add,
        .evt_child_add = _on_child_add,
        .evt_child_remove = _on_child_remove
};

static uint32_t _refcount(struct light_object *obj)
{
        // read through the same type the field is declared as, rather than assuming it is a
        // plain integer -- it is an atomic on every port that has real atomics
        return (uint32_t)obj->ref_count;
}

// --- reference counting ---------------------------------------------------------------------

// a freshly initialised object is owned by whoever initialised it: one reference, not zero.
// zero would mean "already dead", and light_object_get() refuses to resurrect such an object
static void test_init_sets_one_reference(void)
{
        struct light_object obj;
        light_object_init(&obj, &_type_plain);
        CHECK(_refcount(&obj) == 1, "init left ref_count at %u, expected 1", _refcount(&obj));
}

static void test_get_increments(void)
{
        struct light_object obj;
        light_object_init(&obj, &_type_plain);

        struct light_object *ref = light_object_get(&obj);
        CHECK(ref == &obj, "get returned %p, expected the object itself (%p)", (void *)ref, (void *)&obj);
        CHECK(_refcount(&obj) == 2, "after one get, ref_count is %u, expected 2", _refcount(&obj));

        light_object_get(&obj);
        CHECK(_refcount(&obj) == 3, "after two gets, ref_count is %u, expected 3", _refcount(&obj));
}

static void test_put_decrements(void)
{
        struct light_object obj;
        light_object_init(&obj, &_type_plain);
        light_object_get(&obj);
        light_object_get(&obj);
        CHECK(_refcount(&obj) == 3, "setup: ref_count is %u, expected 3", _refcount(&obj));

        //   ONE decrement per put. Worth stating explicitly because put's compare-exchange
        // loop spins `while(status)` -- while the exchange SUCCEEDS -- which reads like it
        // would decrement repeatedly. It does not: the second iteration's expected value is
        // now stale, so the exchange fails and the loop ends. This test is what would catch
        // that reasoning being wrong
        light_object_put(&obj);
        CHECK(_refcount(&obj) == 2, "after one put, ref_count is %u, expected 2", _refcount(&obj));

        light_object_put(&obj);
        CHECK(_refcount(&obj) == 1, "after two puts, ref_count is %u, expected 1", _refcount(&obj));
}

// get and put have to be exact inverses, or a long-running system leaks references until
// nothing can be released, which is invisible until it is not
static void test_get_put_balanced(void)
{
        struct light_object obj;
        light_object_init(&obj, &_type_plain);

        for(int i = 0; i < 100; i++)
                light_object_get(&obj);
        CHECK(_refcount(&obj) == 101, "after 100 gets, ref_count is %u, expected 101", _refcount(&obj));

        for(int i = 0; i < 100; i++)
                light_object_put(&obj);
        CHECK(_refcount(&obj) == 1, "after 100 balanced puts, ref_count is %u, expected 1", _refcount(&obj));
}

//   THE IMPORTANT ONE. An object whose count has reached zero is dead, and get() must refuse
// it rather than hand back a pointer to something already released. Returning non-NULL here is
// a use-after-free the caller cannot detect
static void test_get_on_dead_object_returns_null(void)
{
        struct light_object obj;
        light_object_init(&obj, &_type_plain);
        light_object_put(&obj);
        CHECK(_refcount(&obj) == 0, "setup: ref_count is %u, expected 0", _refcount(&obj));

        struct light_object *ref = light_object_get(&obj);
        CHECK(ref == NULL, "get on a zero-count object returned %p, expected NULL", (void *)ref);
        CHECK(_refcount(&obj) == 0, "a refused get changed ref_count to %u, expected it left 0",
                        _refcount(&obj));
}

//   put() at zero must not wrap. On an unsigned count, 0 - 1 is a very large number, which
// would make the object look permanently, unreleasably alive -- the opposite of the leak the
// naive reading suggests, and much harder to spot
static void test_put_does_not_underflow(void)
{
        struct light_object obj;
        light_object_init(&obj, &_type_plain);
        light_object_put(&obj);
        CHECK(_refcount(&obj) == 0, "setup: ref_count is %u, expected 0", _refcount(&obj));

        light_object_put(&obj);
        CHECK(_refcount(&obj) == 0, "put at zero left ref_count at %u, expected it to stay 0",
                        _refcount(&obj));
        light_object_put(&obj);
        CHECK(_refcount(&obj) == 0, "a second put at zero left ref_count at %u, expected 0",
                        _refcount(&obj));
}

// get(NULL) is reachable whenever a caller passes an optional parent through, so it has to be
// a defined no-op rather than a crash
static void test_get_null_is_safe(void)
{
        struct light_object *ref = light_object_get(NULL);
        CHECK(ref == NULL, "get(NULL) returned %p, expected NULL", (void *)ref);
}

// --- naming and the object tree -------------------------------------------------------------

static void test_add_sets_name_and_parent(void)
{
        struct light_object parent, child;
        light_object_init(&parent, &_type_plain);
        light_object_init(&child, &_type_plain);

        int rc = light_object_add(&child, &parent, (const uint8_t *)"child_%d", 7);
        CHECK(rc == LIGHT_OK, "add returned %d, expected LIGHT_OK", rc);
        CHECK(strcmp((const char *)light_object_get_name(&child), "child_7") == 0,
                        "name is '%s', expected 'child_7'", (const char *)light_object_get_name(&child));
        CHECK(child.parent == &parent, "parent is %p, expected %p",
                        (void *)child.parent, (void *)&parent);
}

//   adding takes a reference on the parent, which is what stops a parent being released while
// a child still points at it
static void test_add_takes_parent_reference(void)
{
        struct light_object parent, child;
        light_object_init(&parent, &_type_plain);
        light_object_init(&child, &_type_plain);
        CHECK(_refcount(&parent) == 1, "setup: parent ref_count is %u, expected 1", _refcount(&parent));

        light_object_add(&child, &parent, (const uint8_t *)"child");
        CHECK(_refcount(&parent) == 2, "after add, parent ref_count is %u, expected 2 (the child's)",
                        _refcount(&parent));
}

// an empty name is rejected rather than silently producing an unnamed object, which would be
// indistinguishable from a working one until something tried to look it up
static void test_add_rejects_empty_name(void)
{
        struct light_object parent, child;
        light_object_init(&parent, &_type_plain);
        light_object_init(&child, &_type_plain);

        int rc = light_object_add(&child, &parent, (const uint8_t *)"");
        CHECK(rc == LIGHT_INVALID, "add with an empty name returned %d, expected LIGHT_INVALID (%d)",
                        rc, LIGHT_INVALID);
}

// a name longer than the field is truncated, not written past the end of the object
static void test_add_truncates_long_name(void)
{
        struct light_object parent, child;
        light_object_init(&parent, &_type_plain);
        light_object_init(&child, &_type_plain);

        char longname[LIGHT_OBJ_NAME_LENGTH * 2];
        memset(longname, 'x', sizeof(longname) - 1);
        longname[sizeof(longname) - 1] = '\0';

        light_object_add(&child, &parent, (const uint8_t *)longname);
        size_t len = strlen((const char *)light_object_get_name(&child));
        CHECK(len == LIGHT_OBJ_NAME_LENGTH - 1,
                        "truncated name is %zu chars, expected %d (the field less its NUL)",
                        len, LIGHT_OBJ_NAME_LENGTH - 1);
}

// the add path is what device libraries hang their initialisation off, so the callbacks firing
// -- and firing once -- is part of the contract rather than an implementation detail
static void test_add_fires_events(void)
{
        struct light_object parent, child;
        _add_calls = _child_add_calls = 0;
        light_object_init(&parent, &_type_events);
        light_object_init(&child, &_type_events);

        light_object_add(&child, &parent, (const uint8_t *)"child");
        CHECK(_add_calls == 1, "evt_add fired %d times, expected 1", _add_calls);
        CHECK(_child_add_calls == 1, "evt_child_add fired %d times, expected 1", _child_add_calls);
}

// --- harness ---------------------------------------------------------------------------------

struct test_case {
        const char *name;
        void (*fn)(void);
};
static const struct test_case test_cases[] = {
        { "init_sets_one_reference",        test_init_sets_one_reference },
        { "get_increments",                 test_get_increments },
        { "put_decrements",                 test_put_decrements },
        { "get_put_balanced",               test_get_put_balanced },
        { "get_on_dead_object_returns_null", test_get_on_dead_object_returns_null },
        { "put_does_not_underflow",         test_put_does_not_underflow },
        { "get_null_is_safe",               test_get_null_is_safe },
        { "add_sets_name_and_parent",       test_add_sets_name_and_parent },
        { "add_takes_parent_reference",     test_add_takes_parent_reference },
        { "add_rejects_empty_name",         test_add_rejects_empty_name },
        { "add_truncates_long_name",        test_add_truncates_long_name },
        { "add_fires_events",               test_add_fires_events },
};
#define TEST_CASE_COUNT (sizeof(test_cases) / sizeof(test_cases[0]))

int main(int argc, char **argv)
{
        //   the registry holds a mutex guarding the object tree, and light_object_add() reaches
        // the default registry directly rather than through the accessor that checks whether it
        // is loaded. light_framework_init() would do this; these tests deliberately do not run
        // the framework, so they have to do it themselves. Without it the add cases lock an
        // uninitialised mutex, which happens to work wherever a zeroed pthread_mutex_t is a
        // valid static initialiser -- not a property worth depending on
        light_core_impl_setup();

        if(argc > 1 && strcmp(argv[1], "--list") == 0) {
                for(size_t i = 0; i < TEST_CASE_COUNT; i++)
                        printf("%s\n", test_cases[i].name);
                return 0;
        }

        if(argc > 1) {
                for(size_t i = 0; i < TEST_CASE_COUNT; i++) {
                        if(strcmp(argv[1], test_cases[i].name) != 0)
                                continue;
                        test_cases[i].fn();
                        printf("%s: %s, %d failure(s)\n", test_cases[i].name,
                               failures ? "FAILED" : "PASSED", failures);
                        return failures ? 1 : 0;
                }
                // an unknown name is an error, not a silent pass: a typo in CMakeLists.txt
                // would otherwise register a test that always succeeds
                printf("FAIL: no such test case '%s'\n", argv[1]);
                return 2;
        }

        for(size_t i = 0; i < TEST_CASE_COUNT; i++)
                test_cases[i].fn();
        printf("%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
        return failures ? 1 : 0;
}
