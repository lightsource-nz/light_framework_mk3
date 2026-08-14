/*
 *  test_light_object_concurrent.c
 *  concurrent behaviour of reference counting in the light object model
 *
 *  HOST ONLY, and separate from test_light_object.c on purpose: that suite is single-threaded
 *  and says so, and these tests need C11 <threads.h>, which only the host port has
 *  (LIGHT_PLATFORM_HAS_C11_THREADS).
 *
 *  ON TESTING RACES. A concurrent test that passes has proved much less than a sequential one
 *  that passes: the interleaving that breaks the code may simply not have happened this run.
 *  Three things here are aimed at that, and none of them are decoration:
 *
 *    - every case asserts an EXACT arithmetic invariant, not a range. N threads issuing K
 *      references between them must leave exactly N*K references, whatever order they ran in.
 *      A correct implementation hits the number every time; a racy one drifts off it. There is
 *      no tolerance to hide in.
 *    - the work is repeated for ROUND_COUNT rounds and every round is checked, so a race with a
 *      low per-round hit rate still shows up, and the report says how many rounds saw it. One
 *      bad round out of twenty is a failure, not noise.
 *    - the threads are released from a spin barrier rather than started and left to drift, so
 *      they overlap instead of running one after another on a fast machine.
 *
 *  A failure here is therefore meaningful in one direction only: it proves a race exists. A
 *  pass does NOT prove the code is race-free -- it only means this harness did not provoke one.
 *  test_concurrent_control_independent_objects is the guard against the opposite error, a suite
 *  that "detects" a race that is really a bug in the test.
 */
#include <light.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <threads.h>

//   light_core references `this_app`, so anything linking it has to define an application. It
// is never started -- main() calls the test functions directly
static void _test_app_event(const struct light_module *module, uint8_t event, void *arg) {}
static uint8_t _test_app_main(struct light_application *app) { return LF_STATUS_RUN; }
Light_Application_Define(test_light_object_concurrent, _test_app_event, _test_app_main, &light_core);

static int failures;

#define CHECK(cond, ...) do { \
        if(!(cond)) { \
                failures++; \
                printf("  FAIL %s:%d: ", __func__, __LINE__); \
                printf(__VA_ARGS__); \
                printf("\n"); \
        } \
} while(0)

// a type with no callbacks: these tests are about the refcount word itself, and a callback
// firing from several threads would be a second, separate question
static struct lobj_type _type_plain = { .id = "test:plain" };

static uint32_t _refcount(struct light_object *obj)
{
        return (uint32_t)atomic_load(&obj->ref_count);
}

//   THREAD_COUNT above the core count on purpose: oversubscribing forces the scheduler to
// preempt threads mid-operation, which is exactly the window a compare-exchange loop has to
// survive. ITERATION_COUNT and ROUND_COUNT are a compromise -- large enough that a race with a
// low hit rate still lands, small enough that the whole suite is a few seconds
#define THREAD_COUNT    8
#define ITERATION_COUNT 20000
#define ROUND_COUNT     20

// --- thread plumbing --------------------------------------------------------------------------

static atomic_int _barrier;

//   released together rather than started and left to run: thrd_create is slow enough that the
// first thread can finish its whole loop before the last one exists, in which case nothing has
// actually run concurrently and the test is measuring nothing.
//
//   spun rather than yielded because the host toolchain's <threads.h> is a shim over pthreads
// that provides thrd_create/thrd_join/thrd_sleep but no thrd_yield. A short sleep after a spin
// budget keeps a late-starting thread from being starved by the ones already spinning, without
// needing a primitive that is not there
static void _barrier_wait(void)
{
        atomic_fetch_add(&_barrier, 1);
        for(unsigned spins = 0; atomic_load(&_barrier) < THREAD_COUNT; spins++) {
                if(spins > 10000) {
                        struct timespec nap = { .tv_sec = 0, .tv_nsec = 1000000 };
                        thrd_sleep(&nap, NULL);
                        spins = 0;
                }
        }
}

static void _run_threads(int (*fn)(void *), void *arg)
{
        thrd_t threads[THREAD_COUNT];
        int started = 0;

        atomic_store(&_barrier, 0);
        for(int i = 0; i < THREAD_COUNT; i++) {
                if(thrd_create(&threads[i], fn, arg) != thrd_success)
                        break;
                started++;
        }
        //   the barrier waits for THREAD_COUNT arrivals, so a short launch would hang every
        // thread that did start. Fail loudly instead of deadlocking the test run
        if(started != THREAD_COUNT) {
                printf("  FATAL: only %d of %d threads started\n", started, THREAD_COUNT);
                exit(3);
        }
        for(int i = 0; i < THREAD_COUNT; i++)
                thrd_join(threads[i], NULL);
        (void)arg;
}

// the object every contended case hammers; one shared word is the whole point
static struct light_object _shared;

// hands each thread a distinct index, for the cases that give every thread its own object
static atomic_int _next_slot;

// --- get() under contention -------------------------------------------------------------------

static int _worker_get(void *arg)
{
        _barrier_wait();
        for(int i = 0; i < ITERATION_COUNT; i++)
                light_object_get(&_shared);
        return 0;
}

//   N threads taking K references each must produce exactly N*K references. Any shortfall is a
// lost increment: two threads read the same old value and one of the two updates is discarded,
// so an object is released while a reference to it is still held -- a use-after-free whose
// cause is nowhere near where it crashes
static void test_concurrent_get_is_atomic(void)
{
        int bad_rounds = 0;
        uint32_t expect = 1 + (uint32_t)THREAD_COUNT * ITERATION_COUNT;

        for(int round = 0; round < ROUND_COUNT; round++) {
                light_object_init(&_shared, &_type_plain);
                _run_threads(_worker_get, NULL);
                uint32_t got = _refcount(&_shared);
                if(got != expect) {
                        if(bad_rounds < 3)
                                printf("  round %d: ref_count %u, expected %u (drift %+d)\n",
                                                round, got, expect, (int)got - (int)expect);
                        bad_rounds++;
                }
        }
        CHECK(bad_rounds == 0, "%d of %d rounds lost or gained references across %d threads",
                        bad_rounds, ROUND_COUNT, THREAD_COUNT);
}

// --- put() under contention -------------------------------------------------------------------

static int _worker_put(void *arg)
{
        _barrier_wait();
        for(int i = 0; i < ITERATION_COUNT; i++)
                light_object_put(&_shared);
        return 0;
}

//   the mirror of the above, and the one most likely to catch a compare-exchange loop that
// gives up instead of retrying. A put that quietly does nothing leaks a reference: the object
// is never released, which is invisible until the system runs out of whatever it was holding
static void test_concurrent_put_is_atomic(void)
{
        int bad_rounds = 0;
        uint32_t start = 1 + (uint32_t)THREAD_COUNT * ITERATION_COUNT;

        for(int round = 0; round < ROUND_COUNT; round++) {
                light_object_init(&_shared, &_type_plain);
                // charge the count single-threaded, so this case tests put() alone rather than
                // failing because get() already drifted
                atomic_store(&_shared.ref_count, start);
                _run_threads(_worker_put, NULL);
                uint32_t got = _refcount(&_shared);
                if(got != 1) {
                        if(bad_rounds < 3)
                                printf("  round %d: ref_count %u, expected 1 (drift %+d)\n",
                                                round, got, (int)got - 1);
                        bad_rounds++;
                }
        }
        CHECK(bad_rounds == 0, "%d of %d rounds lost or duplicated decrements across %d threads",
                        bad_rounds, ROUND_COUNT, THREAD_COUNT);
}

// --- the pairing that real code actually performs ---------------------------------------------

static int _worker_get_put(void *arg)
{
        _barrier_wait();
        for(int i = 0; i < ITERATION_COUNT; i++) {
                light_object_get(&_shared);
                light_object_put(&_shared);
        }
        return 0;
}

//   THE ONE THAT MATTERS. Every thread is balanced -- one put for every get -- so however the
// threads interleave, the count has to come back to where it started. This is the shape of all
// real use: take a reference, use it, drop it.
//
//   Which direction it drifts identifies the fault. Upward means puts are being lost (a leak);
// downward means decrements are being applied twice or increments lost (a premature free,
// which is the dangerous one), so the drift is reported rather than just the mismatch
static void test_concurrent_get_put_balanced(void)
{
        int bad_rounds = 0;
        int worst_low = 0, worst_high = 0;

        for(int round = 0; round < ROUND_COUNT; round++) {
                light_object_init(&_shared, &_type_plain);
                _run_threads(_worker_get_put, NULL);
                int drift = (int)_refcount(&_shared) - 1;
                if(drift) {
                        if(drift < worst_low) worst_low = drift;
                        if(drift > worst_high) worst_high = drift;
                        bad_rounds++;
                }
        }
        CHECK(bad_rounds == 0,
                        "%d of %d rounds ended with an unbalanced count (drift %+d..%+d); "
                        "positive drift leaks references, negative frees them early",
                        bad_rounds, ROUND_COUNT, worst_low, worst_high);
}

// --- the count must not wrap ------------------------------------------------------------------

static int _worker_put_past_zero(void *arg)
{
        _barrier_wait();
        for(int i = 0; i < ITERATION_COUNT; i++)
                light_object_put(&_shared);
        return 0;
}

//   deliberately far more puts than there are references, from every thread at once. The
// guard in put() is `if(count > 0)`, read and acted on non-atomically, so two threads can both
// read 1 and both decrement.
//
//   This case asserts only that the count never WRAPS, not that it lands on zero: on an
// unsigned word 0 - 1 is four billion, which makes a dead object look permanently alive and is
// a far worse outcome than being a few short. Landing exactly on zero is what
// test_concurrent_put_is_atomic covers, and asserting it here too would just report the same
// fault twice under a name that does not describe it
static void test_concurrent_put_does_not_wrap(void)
{
        int bad_rounds = 0;
        uint32_t start = 1 + (uint32_t)THREAD_COUNT * (ITERATION_COUNT / 2);

        for(int round = 0; round < ROUND_COUNT; round++) {
                light_object_init(&_shared, &_type_plain);
                atomic_store(&_shared.ref_count, start);
                _run_threads(_worker_put_past_zero, NULL);
                uint32_t got = _refcount(&_shared);
                // anything above the starting value can only have come from wrapping: nothing
                // in this case ever increments
                if(got > start) {
                        if(bad_rounds < 3)
                                printf("  round %d: ref_count %u, above the starting %u -- wrapped\n",
                                                round, got, start);
                        bad_rounds++;
                }
        }
        CHECK(bad_rounds == 0, "%d of %d rounds wrapped the reference count past zero",
                        bad_rounds, ROUND_COUNT);
}

// --- a dead object must stay dead -------------------------------------------------------------

static atomic_int _observed_death;
static atomic_int _resurrections;

static int _worker_get_racing_death(void *arg)
{
        _barrier_wait();
        for(int i = 0; i < ITERATION_COUNT; i++) {
                struct light_object *ref = light_object_get(&_shared);
                if(ref) {
                        //   a successful get AFTER any thread has seen the object dead. This
                        // workload is net-decreasing, so the count can never legitimately rise
                        // back off zero -- no assumption about thread ordering is needed to
                        // call this wrong
                        if(atomic_load(&_observed_death))
                                atomic_fetch_add(&_resurrections, 1);
                        //   TWO puts against the one get, so every iteration nets -1 and the
                        // object is actively driven towards death while other threads are
                        // still trying to get it. A balanced get/put loop would hold the count
                        // steady forever and never reach the path this case exists to test
                        light_object_put(&_shared);
                        light_object_put(&_shared);
                } else {
                        atomic_store(&_observed_death, 1);
                }
        }
        return 0;
}

//   get() refusing a zero-count object is the single most safety-relevant thing in the whole
// object model: handing back a pointer to something already released is a use-after-free the
// caller has no way to detect. Sequentially that is test_get_on_dead_object_returns_null; this
// is the same property with a live race against the object dying.
//
//   HONEST LIMIT, stated because a green result here is easy to over-read: if the object never
// reaches zero then no thread ever sees death, no get is ever refused, and this case passes
// without having tested anything at all. The workload is built to drain the count precisely so
// that cannot happen quietly, and the case reports whether death was actually observed rather
// than leaving it to be assumed
static void test_concurrent_get_rejects_dead_object(void)
{
        int rounds_that_died = 0;

        atomic_store(&_resurrections, 0);
        for(int round = 0; round < ROUND_COUNT; round++) {
                light_object_init(&_shared, &_type_plain);
                //   a small starting charge: the workload nets -1 per iteration, so this is
                // spent within the first handful of iterations and the remaining tens of
                // thousands are all gets racing an object that is already dead
                atomic_store(&_shared.ref_count, 1 + (uint32_t)THREAD_COUNT);
                atomic_store(&_observed_death, 0);
                _run_threads(_worker_get_racing_death, NULL);
                if(atomic_load(&_observed_death))
                        rounds_that_died++;
        }
        CHECK(atomic_load(&_resurrections) == 0,
                        "get() returned a live pointer %d times for an object already seen dead",
                        atomic_load(&_resurrections));
        if(!rounds_that_died)
                printf("  NOTE: the object never reached zero in any round -- this case did not "
                                "exercise the dead-object path\n");
}

// --- the object tree --------------------------------------------------------------------------

static struct light_object _tree_parent;
static struct light_object _tree_child;

//   ONE attach raced by THREAD_COUNT detaches, checked exactly, repeated. Getting to this
// design took three failed attempts, each of which passed no matter what the implementation
// did -- worth recording so none of them gets rebuilt:
//
//   1. one child per thread. obj->parent was then per-thread, leaving the parent's reference
//      count as the only shared state -- and that is protected by the lock-free compare-
//      exchange whatever the tree lock does.
//   2. every thread doing add-then-del. A del releases at most once per call, so releases
//      could never cumulatively exceed acquisitions and the count could not be driven down.
//   3. one adder against N deleters, asserting the count never reaches zero. This did detect
//      the fault -- but only sometimes, and it got WORSE as iterations rose: an add over an
//      existing link leaks a reference, so long runs inflate the count away from zero and the
//      signal goes deaf. A detector that degrades with more sampling is measuring the wrong
//      thing.
//
//   The fix is to stop sampling and start checking: re-establish exactly one link, release
// every thread at it at once, wait for all of them, and assert the count came back to exactly
// one. Two threads consuming the same link is then a caught violation rather than a drift that
// may or may not show up in the total
#define TREE_SUBROUND_COUNT 4000

static atomic_int _tree_phase;
static atomic_int _tree_arrived;

static int _worker_tree_del(void *arg)
{
        for(int sub = 1; sub <= TREE_SUBROUND_COUNT; sub++) {
                // wait to be released at this sub-round's freshly attached link
                while(atomic_load(&_tree_phase) < sub)
                        ;
                light_object_del(&_tree_child);
                atomic_fetch_add(&_tree_arrived, 1);
        }
        return 0;
}

//   del() reads the parent link, clears it, and releases the reference that link stood for.
// Those three steps have to be one indivisible act: if two threads both read the same non-NULL
// parent before either clears it, both go on to release it, and the parent loses two
// references for the one that was taken.
//
//   The count drifting UP here is legitimate -- two adds in a row genuinely take two
// references and leave one link, so a balanced call count does not imply a balanced reference
// count, and asserting equality would be wrong. What can never happen is the parent falling
// below the one reference it holds on itself: every release must correspond to a reference
// somebody took. Reaching zero means an object was freed while still referenced
static void test_concurrent_tree_no_double_release(void)
{
        thrd_t threads[THREAD_COUNT];
        int violations = 0;

        light_object_init(&_tree_parent, &_type_plain);
        light_object_init(&_tree_child, &_type_plain);
        //   light_object_init() sets the reference count and the type and NOTHING else -- it
        // leaves obj->parent exactly as it was. A stale link here would be detached by threads
        // that never attached it, which reads exactly like the bug this case hunts for
        _tree_child.parent = NULL;
        atomic_store(&_tree_phase, 0);
        atomic_store(&_tree_arrived, 0);

        for(int i = 0; i < THREAD_COUNT; i++) {
                if(thrd_create(&threads[i], _worker_tree_del, NULL) == thrd_success)
                        continue;
                printf("  FATAL: could not start thread %d\n", i);
                exit(3);
        }

        for(int sub = 1; sub <= TREE_SUBROUND_COUNT; sub++) {
                // exactly one link, and one reference on the parent to go with it: 2 total
                light_object_add(&_tree_child, &_tree_parent, (const uint8_t *)"child");

                atomic_store(&_tree_arrived, 0);
                atomic_store(&_tree_phase, sub);
                while(atomic_load(&_tree_arrived) < THREAD_COUNT)
                        ;

                //   exactly one of those detaches may consume the link, so the parent is back
                // to the single reference it holds on itself. Anything less means the link was
                // consumed more than once and a reference was released that nobody took
                uint32_t got = _refcount(&_tree_parent);
                if(got != 1) {
                        if(violations < 3)
                                printf("  sub-round %d: parent ref_count %u, expected 1 -- the "
                                                "link was consumed %d times\n", sub, got, 2 - (int)got);
                        violations++;
                }

                // reset, so one violation does not cascade into every sub-round after it
                atomic_store(&_tree_parent.ref_count, 1);
                _tree_child.parent = NULL;
        }

        for(int i = 0; i < THREAD_COUNT; i++)
                thrd_join(threads[i], NULL);

        CHECK(violations == 0, "%d of %d attaches were detached more than once by %d racing "
                        "threads", violations, TREE_SUBROUND_COUNT, THREAD_COUNT);
}

// --- control ----------------------------------------------------------------------------------

static struct light_object _private[THREAD_COUNT];

static int _worker_private(void *arg)
{
        struct light_object *obj = &_private[atomic_fetch_add(&_next_slot, 1)];
        _barrier_wait();
        for(int i = 0; i < ITERATION_COUNT; i++) {
                light_object_get(obj);
                light_object_put(obj);
        }
        return 0;
}

//   THE CONTROL, and the reason the failures above can be believed. Same threads, same call
// counts, same everything -- except each thread owns its object, so there is no contention on
// any one count. If this fails too, the fault is in the harness (or in get/put generally) and
// the contended results say nothing about concurrency. If this passes while the contended
// cases fail, the difference between them IS the concurrency, which is precisely the claim
static void test_concurrent_control_independent_objects(void)
{
        int bad = 0;

        for(int round = 0; round < ROUND_COUNT; round++) {
                for(int i = 0; i < THREAD_COUNT; i++)
                        light_object_init(&_private[i], &_type_plain);
                atomic_store(&_next_slot, 0);
                _run_threads(_worker_private, NULL);
                for(int i = 0; i < THREAD_COUNT; i++) {
                        if(_refcount(&_private[i]) == 1)
                                continue;
                        if(bad < 3)
                                printf("  round %d thread %d: ref_count %u, expected 1\n",
                                                round, i, _refcount(&_private[i]));
                        bad++;
                }
        }
        CHECK(bad == 0, "%d uncontended objects ended with the wrong count -- the fault is not "
                        "contention", bad);
}

// --- harness -----------------------------------------------------------------------------------

struct test_case {
        const char *name;
        void (*fn)(void);
};
static const struct test_case test_cases[] = {
        { "control_independent_objects",  test_concurrent_control_independent_objects },
        { "get_is_atomic",                test_concurrent_get_is_atomic },
        { "put_is_atomic",                test_concurrent_put_is_atomic },
        { "get_put_balanced",             test_concurrent_get_put_balanced },
        { "put_does_not_wrap",            test_concurrent_put_does_not_wrap },
        { "get_rejects_dead_object",      test_concurrent_get_rejects_dead_object },
        { "tree_no_double_release",       test_concurrent_tree_no_double_release },
};
#define TEST_CASE_COUNT (sizeof(test_cases) / sizeof(test_cases[0]))

int main(int argc, char **argv)
{
        //   see test_light_object.c: the registry's tree mutex has to be initialised, and
        // light_framework_init() is what would normally do it
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

        for(size_t i = 0; i < TEST_CASE_COUNT; i++) {
                printf("-- %s\n", test_cases[i].name);
                test_cases[i].fn();
        }
        printf("%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
        return failures ? 1 : 0;
}
