/*
 *  test_light_list.c
 *  the array-list helpers in light_core: append, insert, indexof, and the two deletes
 *
 *  HOST ONLY, though nothing here is host-specific -- these are pure operations on a caller's
 *  array and count, with no allocation, no locking and no platform calls. They run on the host
 *  because that is where tests run at all.
 *
 *  WHY THIS SUITE EXISTS. Coverage put list.c at 31% line, the lowest in light_core, and this
 *  is code the framework itself relies on: the module registry, the loader table and the
 *  context object list are all array-lists. What was uncovered was every path that MOVES
 *  elements -- insert and both deletes -- and all of it was broken.
 *
 *  WHAT THE UNCOVERED PATHS TURNED OUT TO DO:
 *
 *    - delete_at_index() wrote `(*list)[*count--] = NULL`, which parses as `*(count--)`. That
 *      decrements the POINTER, a local copy that is then discarded, so the caller's count was
 *      never reduced; and it stored NULL at the ORIGINAL count index, one past the last
 *      element -- an out-of-bounds write for a full array.
 *    - insert() shifted the wrong way. Copying (*list)[i] to (*list)[i + 1] while i ascends
 *      propagates the element at `index` across every slot after it, so a single insert into
 *      the middle replaced the entire tail with one repeated value.
 *
 *  Both are the kind of fault that only shows once a list is used in anger, and neither had a
 *  test. The cases below pin the contract each caller already assumes.
 *
 *  MUTATION-CHECKED, by mutants.ps1 beside this file: eight mutants, including both original
 *  defects verbatim, and all eight are killed. The bounds check on delete_at_index survived the
 *  first run -- nothing deleted at an out-of-range index -- which is what
 *  delete_at_index_ignores_an_out_of_range_index was added for, and it is the case that covers
 *  an empty list, where `*count - 1` underflows a uint8_t to 255 and the shift walks off the
 *  end of the caller's array.
 *
 *  ONE LIMIT WORTH KNOWING. delete_item_ignores_an_absent_item cannot distinguish "looked and
 *  found nothing" from "did nothing at all": an implementation with an empty body would pass it
 *  too. The cases above it are what establish that deletion works when the item IS present, and
 *  the case is kept for the no-corruption half of the contract rather than for the lookup.
 */
#include <light.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//   light_core references `this_app`, so anything linking it has to define an application. It
// is never started -- main() calls the test functions directly
static void _test_app_event(const struct light_module *module, uint8_t event, void *arg) {}
static uint8_t _test_app_main(struct light_application *app) { return LF_STATUS_RUN; }
Light_Application_Define(test_light_list, _test_app_event, _test_app_main, &light_core);

static int failures;

#define CHECK(cond, ...) do { \
        if(!(cond)) { \
                failures++; \
                printf("  FAIL %s:%d: ", __func__, __LINE__); \
                printf(__VA_ARGS__); \
                printf("\n"); \
        } \
} while(0)

//   distinct addresses to store; their values never matter, only their identity. Statics rather
// than allocations so a failure cannot be confused with an allocator problem
static int _a, _b, _c, _d;
#define A ((void *)&_a)
#define B ((void *)&_b)
#define C ((void *)&_c)
#define D ((void *)&_d)

// a slot past the ones a case fills, so an off-by-one write lands somewhere a test can see it
#define LIST_CAP 8

struct fixture {
        void *list[LIST_CAP];
        uint8_t count;
};

static void fixture_init(struct fixture *f, uint8_t n)
{
        void *items[4] = { A, B, C, D };
        memset(f, 0, sizeof(*f));
        for(uint8_t i = 0; i < n && i < 4; i++)
                light_arraylist_append(&f->list, &f->count, items[i]);
}

// --- indexof ---------------------------------------------------------------------------------

static void test_indexof_finds_each_position(void)
{
        struct fixture f;
        fixture_init(&f, 4);

        CHECK(light_arraylist_indexof(&f.list, f.count, A) == 0, "expected A at 0");
        CHECK(light_arraylist_indexof(&f.list, f.count, B) == 1, "expected B at 1");
        CHECK(light_arraylist_indexof(&f.list, f.count, C) == 2, "expected C at 2");
        CHECK(light_arraylist_indexof(&f.list, f.count, D) == 3, "expected D at 3");
}
static void test_indexof_reports_absent_as_negative(void)
{
        struct fixture f;
        fixture_init(&f, 2);

        //   -1 rather than 0: callers test `>= 0`, so returning an index for something that is
        // not present would delete or replace the wrong element
        CHECK(light_arraylist_indexof(&f.list, f.count, C) == -1, "absent item should give -1");
}
static void test_indexof_respects_the_count(void)
{
        struct fixture f;
        fixture_init(&f, 4);

        //   the search must stop at `count`, not at the end of the array. Anything beyond the
        // count is stale or uninitialised and must not be matched
        CHECK(light_arraylist_indexof(&f.list, 2, C) == -1,
                "C is beyond count=2 and must not be found");
}

// --- append ----------------------------------------------------------------------------------

static void test_append_adds_in_order(void)
{
        struct fixture f;
        fixture_init(&f, 3);

        CHECK(f.count == 3, "count should be 3, got %u", f.count);
        CHECK(f.list[0] == A && f.list[1] == B && f.list[2] == C, "append order wrong");
        CHECK(f.list[3] == NULL, "append wrote past the end");
}

// --- delete_at_index -------------------------------------------------------------------------

static void test_delete_at_index_shortens_the_list(void)
{
        struct fixture f;
        fixture_init(&f, 3);

        light_arraylist_delete_at_index(&f.list, &f.count, 1);

        //   the count is the whole point: every caller iterates `i < count`, so a delete that
        // leaves it unchanged leaves a duplicate of the last element visible to everyone
        CHECK(f.count == 2, "count should be 2 after deleting one of three, got %u", f.count);
        CHECK(f.list[0] == A, "element before the deleted one should be untouched");
        CHECK(f.list[1] == C, "element after the deleted one should have moved down");
}
static void test_delete_at_index_first_and_last(void)
{
        struct fixture f;

        fixture_init(&f, 3);
        light_arraylist_delete_at_index(&f.list, &f.count, 0);
        CHECK(f.count == 2 && f.list[0] == B && f.list[1] == C, "deleting the first element");

        fixture_init(&f, 3);
        light_arraylist_delete_at_index(&f.list, &f.count, 2);
        CHECK(f.count == 2 && f.list[0] == A && f.list[1] == B, "deleting the last element");
}
static void test_delete_at_index_stays_in_bounds(void)
{
        struct fixture f;
        //   fills the array completely, so writing at the old count -- which is what
        // `(*list)[*count--]` did -- lands outside it. LIST_CAP is the array's real size, so
        // this case is the one that would have caught that as a buffer overrun rather than as
        // a wrong value
        fixture_init(&f, 4);
        for(uint8_t i = 4; i < LIST_CAP; i++)
                light_arraylist_append(&f.list, &f.count, A);
        CHECK(f.count == LIST_CAP, "fixture should be full, got %u", f.count);

        light_arraylist_delete_at_index(&f.list, &f.count, 0);
        CHECK(f.count == LIST_CAP - 1, "count should drop to %u, got %u", LIST_CAP - 1, f.count);
}
static void test_delete_to_empty(void)
{
        struct fixture f;
        fixture_init(&f, 1);

        light_arraylist_delete_at_index(&f.list, &f.count, 0);
        CHECK(f.count == 0, "deleting the only element should empty the list, got %u", f.count);
        CHECK(light_arraylist_indexof(&f.list, f.count, A) == -1, "empty list should find nothing");
}

static void test_delete_at_index_ignores_an_out_of_range_index(void)
{
        struct fixture f;
        fixture_init(&f, 2);

        light_arraylist_delete_at_index(&f.list, &f.count, 5);
        CHECK(f.count == 2, "an index past the end should delete nothing, got count %u", f.count);
        CHECK(f.list[0] == A && f.list[1] == B, "list should be unchanged");

        //   the empty list is the case that matters. Without the guard, `*count - 1` on a count
        // of zero underflows a uint8_t to 255 and the shift loop walks 255 slots off the end of
        // the caller's array, writing as it goes
        memset(&f, 0, sizeof(f));
        light_arraylist_delete_at_index(&f.list, &f.count, 0);
        CHECK(f.count == 0, "deleting from an empty list should leave it empty, got %u", f.count);
        for(uint8_t i = 0; i < LIST_CAP; i++)
                CHECK(f.list[i] == NULL, "slot %u was written on an empty-list delete", i);
}

// --- delete_item -----------------------------------------------------------------------------

static void test_delete_item_removes_only_that_item(void)
{
        struct fixture f;
        fixture_init(&f, 4);

        light_arraylist_delete_item(&f.list, &f.count, C);

        CHECK(f.count == 3, "count should be 3, got %u", f.count);
        CHECK(light_arraylist_indexof(&f.list, f.count, C) == -1, "C should be gone");
        CHECK(light_arraylist_indexof(&f.list, f.count, A) == 0, "A should still be at 0");
        CHECK(light_arraylist_indexof(&f.list, f.count, B) == 1, "B should still be at 1");
        CHECK(light_arraylist_indexof(&f.list, f.count, D) == 2, "D should have moved to 2");
}
static void test_delete_item_ignores_an_absent_item(void)
{
        struct fixture f;
        fixture_init(&f, 2);

        light_arraylist_delete_item(&f.list, &f.count, D);

        //   pins that an absent item is a no-op rather than a corruption. It does NOT prove the
        // lookup happened -- an implementation that did nothing at all would also pass -- and
        // the cases above are what establish that deletion works when the item IS present
        CHECK(f.count == 2, "count should be unchanged at 2, got %u", f.count);
        CHECK(f.list[0] == A && f.list[1] == B, "list should be unchanged");
}

// --- insert ----------------------------------------------------------------------------------

static void test_insert_in_the_middle_keeps_the_tail(void)
{
        struct fixture f;
        fixture_init(&f, 3);

        light_arraylist_insert(&f.list, &f.count, D, 1);

        //   the case the ascending-copy loop failed: it wrote (*list)[i+1] = (*list)[i] with i
        // ascending, so B was copied over C and then over the slot after it, leaving B repeated
        // across the tail. C surviving at the end is the whole assertion
        CHECK(f.count == 4, "count should be 4, got %u", f.count);
        CHECK(f.list[0] == A, "A should still be first");
        CHECK(f.list[1] == D, "D should be at the insert position");
        CHECK(f.list[2] == B, "B should have shifted up");
        CHECK(f.list[3] == C, "C should have shifted up and survived");
}
static void test_insert_at_the_front(void)
{
        struct fixture f;
        fixture_init(&f, 2);

        light_arraylist_insert(&f.list, &f.count, C, 0);

        CHECK(f.count == 3, "count should be 3, got %u", f.count);
        CHECK(f.list[0] == C && f.list[1] == A && f.list[2] == B, "insert at 0 should shift all");
}
static void test_insert_at_the_end_appends(void)
{
        struct fixture f;
        fixture_init(&f, 2);

        light_arraylist_insert(&f.list, &f.count, C, 2);

        CHECK(f.count == 3, "inserting at the end should grow the list, got %u", f.count);
        CHECK(f.list[2] == C, "C should be last");
        CHECK(f.list[0] == A && f.list[1] == B, "existing elements should be untouched");
}
static void test_insert_beyond_the_end_clamps(void)
{
        struct fixture f;
        fixture_init(&f, 2);

        //   an index past the end is clamped to the end rather than leaving a hole. Pinned
        // because the alternative -- writing at the requested index -- would skip slots and
        // leave uninitialised entries below the count, which indexof would then read
        light_arraylist_insert(&f.list, &f.count, C, 6);

        CHECK(f.count == 3, "count should be 3, got %u", f.count);
        CHECK(f.list[2] == C, "C should have been clamped to the end");
        CHECK(f.list[3] == NULL, "nothing should have been written past the new end");
}

// --- dispatch ---------------------------------------------------------------------------------

static const struct { const char *name; void (*fn)(void); } test_cases[] = {
        { "indexof_finds_each_position",          test_indexof_finds_each_position },
        { "indexof_reports_absent_as_negative",   test_indexof_reports_absent_as_negative },
        { "indexof_respects_the_count",           test_indexof_respects_the_count },
        { "append_adds_in_order",                 test_append_adds_in_order },
        { "delete_at_index_shortens_the_list",    test_delete_at_index_shortens_the_list },
        { "delete_at_index_first_and_last",       test_delete_at_index_first_and_last },
        { "delete_at_index_stays_in_bounds",      test_delete_at_index_stays_in_bounds },
        { "delete_to_empty",                      test_delete_to_empty },
        { "delete_at_index_ignores_an_out_of_range_index", test_delete_at_index_ignores_an_out_of_range_index },
        { "delete_item_removes_only_that_item",   test_delete_item_removes_only_that_item },
        { "delete_item_ignores_an_absent_item",   test_delete_item_ignores_an_absent_item },
        { "insert_in_the_middle_keeps_the_tail",  test_insert_in_the_middle_keeps_the_tail },
        { "insert_at_the_front",                  test_insert_at_the_front },
        { "insert_at_the_end_appends",            test_insert_at_the_end_appends },
        { "insert_beyond_the_end_clamps",         test_insert_beyond_the_end_clamps },
};
#define TEST_CASE_COUNT (sizeof(test_cases) / sizeof(test_cases[0]))

int main(int argc, char **argv)
{
        // the object registry's mutex, which light_framework_init() would normally set up
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
