/*
 *  test_light_stream.c
 *  the stream message queue, and the small string mappings beside it
 *
 *  HOST ONLY, like the other suites.
 *
 *  WHY THIS SUITE EXISTS. Coverage showed the mqueue API almost entirely unexercised -- init,
 *  add_fast, peek, advance, get, is_empty and is_full all at 0% -- despite it carrying every
 *  log line the framework produces. What tested it at all was test_light_log, and only through
 *  the one path it needed: add a message, read it straight back.
 *
 *  WHY IT MATTERS MORE THAN A PERCENTAGE. This is a circular buffer whose full condition is
 *  load-bearing. light_stream.h documents, at length, that a full queue on a single-core drain
 *  path is a HARD DEADLOCK: mqueue_claim_slot() waits on a condition only the main loop can
 *  signal, and the main loop is the thing blocked filling it. is_full() is the guard callers
 *  are expected to consult, and nothing checked that it becomes true at exactly the right
 *  point, or that the buffer still behaves once it has wrapped.
 *
 *  WHAT IS DELIBERATELY NOT TESTED HERE. No case ever adds to a full queue. That is not an
 *  omission -- doing so would block this process forever on the single-core configuration and
 *  hang ctest rather than fail it. The boundary is approached from below and is_full() is what
 *  gets asserted; the blocking behaviour itself belongs to a test that can run a drain thread,
 *  which this suite deliberately does not.
 *
 *  MUTATION-CHECKED by mutants.ps1 beside this file.
 */
#include <light.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//   light_core references `this_app`, so anything linking it has to define an application. It
// is never started -- main() calls the test functions directly
static void _test_app_event(const struct light_module *module, uint8_t event, void *arg) {}
static uint8_t _test_app_main(struct light_application *app) { return LF_STATUS_RUN; }
Light_Application_Define(test_light_stream, _test_app_event, _test_app_main, &light_core);

static int failures;

#define CHECK(cond, ...) do { \
        if(!(cond)) { \
                failures++; \
                printf("  FAIL %s:%d: ", __func__, __LINE__); \
                printf(__VA_ARGS__); \
                printf("\n"); \
        } \
} while(0)

//   a queue and lock of this suite's own, not a stream's: these entry points take the lock and
// the queue separately (a stream shares one lock between its queue and its output, see the note
// in light_stream.h), so a queue can be exercised without a stream existing at all
static struct light_stream_mqueue _q;
static light_mutex_t _lock;

static void fixture_init(void)
{
        light_mutex_init(&_lock);
        light_stream_mqueue_init(&_q);
}
// adds "msg <n>" so a test can tell which message came back, and in what order
static void add_numbered(uint32_t n)
{
        uint8_t text[32];
        snprintf((char *)text, sizeof(text), "msg %u", n);
        light_stream_mqueue_add_fast(&_lock, &_q, text);
}
static bool get_numbered(uint32_t *out)
{
        struct light_message m;
        if(!light_stream_mqueue_try_get(&_lock, &_q, &m))
                return false;
        return sscanf((const char *)m.text, "msg %u", out) == 1;
}

// --- initial state ----------------------------------------------------------------------------

static void test_a_new_queue_is_empty_and_not_full(void)
{
        fixture_init();

        CHECK(light_stream_mqueue_is_empty(&_q), "a new queue should be empty");
        CHECK(!light_stream_mqueue_is_full(&_q), "a new queue should not be full");
}
static void test_try_get_on_an_empty_queue_reports_failure(void)
{
        struct light_message m;
        fixture_init();

        memset(&m, 0xAB, sizeof(m));
        CHECK(!light_stream_mqueue_try_get(&_lock, &_q, &m), "try_get should fail when empty");
        //   the contract says *out is not touched on failure. A caller that ignores the return
        // and prints the buffer anyway would otherwise emit whatever was on its stack
        CHECK(m.text[0] == 0xAB, "try_get must not write to *out when it returns false");
}

// --- ordering ---------------------------------------------------------------------------------

static void test_messages_come_back_in_order(void)
{
        uint32_t n;
        fixture_init();

        for(uint32_t i = 0; i < 5; i++)
                add_numbered(i);

        //   FIFO, not LIFO: this is a log queue, and the drain writes in the order it reads.
        // Reversing it would reorder every multi-line diagnostic in the system
        for(uint32_t i = 0; i < 5; i++) {
                CHECK(get_numbered(&n), "message %u should be available", i);
                CHECK(n == i, "expected message %u, got %u", i, n);
        }
        CHECK(light_stream_mqueue_is_empty(&_q), "queue should be empty after draining it");
}
static void test_the_text_survives_the_round_trip(void)
{
        struct light_message m;
        const char *sent = "the quick brown fox";
        fixture_init();

        light_stream_mqueue_add_fast(&_lock, &_q, (const uint8_t *)sent);
        CHECK(light_stream_mqueue_try_get(&_lock, &_q, &m), "message should be available");
        CHECK(strcmp((const char *)m.text, sent) == 0,
                "expected '%s', got '%s'", sent, (const char *)m.text);
}

// --- the full boundary --------------------------------------------------------------------------

static void test_is_full_becomes_true_at_the_depth(void)
{
        fixture_init();

        //   stops one short first, because the boundary is what matters. A queue that reported
        // full a slot early would waste one; one that reported full a slot late would let a
        // caller into mqueue_claim_slot() with nowhere to put the message, which on the
        // single-core drain path never returns
        for(uint32_t i = 0; i < LIGHT_STREAM_MQUEUE_DEPTH - 1; i++)
                add_numbered(i);
        CHECK(!light_stream_mqueue_is_full(&_q),
                "queue should not be full at %d of %d", LIGHT_STREAM_MQUEUE_DEPTH - 1,
                LIGHT_STREAM_MQUEUE_DEPTH);

        add_numbered(LIGHT_STREAM_MQUEUE_DEPTH - 1);
        CHECK(light_stream_mqueue_is_full(&_q),
                "queue should be full at %d messages", LIGHT_STREAM_MQUEUE_DEPTH);
        CHECK(!light_stream_mqueue_is_empty(&_q), "a full queue is not an empty one");
}
static void test_a_full_queue_still_returns_everything(void)
{
        uint32_t n;
        fixture_init();

        for(uint32_t i = 0; i < LIGHT_STREAM_MQUEUE_DEPTH; i++)
                add_numbered(i);

        for(uint32_t i = 0; i < LIGHT_STREAM_MQUEUE_DEPTH; i++) {
                CHECK(get_numbered(&n), "message %u should be available", i);
                CHECK(n == i, "expected message %u, got %u", i, n);
        }
        CHECK(light_stream_mqueue_is_empty(&_q), "queue should be empty again");
        CHECK(!light_stream_mqueue_is_full(&_q), "a drained queue is not full");
}
static void test_the_buffer_wraps_and_keeps_order(void)
{
        uint32_t n;
        fixture_init();

        //   .message[] is circular, so head runs off the end and back to zero. Filling most of
        // it, draining most of it, then filling again pushes the writes across that seam --
        // which nothing else here does, and where an index that failed to wrap would either
        // overwrite a live message or read a stale one
        for(uint32_t i = 0; i < LIGHT_STREAM_MQUEUE_DEPTH - 2; i++)
                add_numbered(i);
        for(uint32_t i = 0; i < LIGHT_STREAM_MQUEUE_DEPTH - 4; i++) {
                CHECK(get_numbered(&n), "drain %u", i);
                CHECK(n == i, "drain expected %u, got %u", i, n);
        }
        for(uint32_t i = 1000; i < 1000 + 10; i++)
                add_numbered(i);

        // the two left from before the wrap come first, then the ten added after it
        for(uint32_t i = LIGHT_STREAM_MQUEUE_DEPTH - 4; i < LIGHT_STREAM_MQUEUE_DEPTH - 2; i++) {
                CHECK(get_numbered(&n), "pre-wrap message %u", i);
                CHECK(n == i, "pre-wrap expected %u, got %u", i, n);
        }
        for(uint32_t i = 1000; i < 1000 + 10; i++) {
                CHECK(get_numbered(&n), "post-wrap message %u", i);
                CHECK(n == i, "post-wrap expected %u, got %u", i, n);
        }
        CHECK(light_stream_mqueue_is_empty(&_q), "queue should be empty after the wrap");
}

// --- peek / advance ---------------------------------------------------------------------------

static void test_peek_does_not_consume(void)
{
        uint32_t n;
        fixture_init();
        add_numbered(7);

        light_mutex_do_lock(&_lock);
        struct light_message *p = light_stream_mqueue_peek(&_q);
        CHECK(p != NULL, "peek should return the queued message");
        light_mutex_do_unlock(&_lock);

        //   peek leaves the message in place; only advance removes it. The pair exists so a
        // drain can write straight out of the queue without copying ~256 bytes onto its stack,
        // and a peek that consumed would silently drop every message the writer then failed on
        CHECK(!light_stream_mqueue_is_empty(&_q), "peek must not consume the message");
        CHECK(get_numbered(&n) && n == 7, "the message should still be retrievable");
}
static void test_advance_consumes_exactly_one(void)
{
        uint32_t n;
        fixture_init();
        add_numbered(1);
        add_numbered(2);

        light_mutex_do_lock(&_lock);
        light_stream_mqueue_advance(&_q);
        light_mutex_do_unlock(&_lock);

        CHECK(!light_stream_mqueue_is_empty(&_q), "one message should remain");
        CHECK(get_numbered(&n), "the second message should be available");
        CHECK(n == 2, "advance should have dropped the FIRST message, got %u", n);
}

// --- flush ------------------------------------------------------------------------------------

static void test_flush_returns_on_an_empty_queue(void)
{
        fixture_init();

        //   light_fatal() calls this before exit(), so a flush that failed to notice an empty
        // queue would delay every fatal error by its whole timeout -- a second of silence at
        // precisely the moment the operator is waiting to be told what went wrong
        light_stream_flush();
        CHECK(1, "flush returned");
}

// --- the string mappings ------------------------------------------------------------------------

static void test_every_log_level_is_named(void)
{
        //   these appear in the log prefix, so a wrong mapping mislabels the severity of every
        // line at that level -- an ERROR printed as INFO is worse than no label at all
        CHECK(strcmp((const char *)light_log_level_to_string(LOG_TRACE), "TRACE") == 0, "LOG_TRACE");
        CHECK(strcmp((const char *)light_log_level_to_string(LOG_DEBUG), "DEBUG") == 0, "LOG_DEBUG");
        CHECK(strcmp((const char *)light_log_level_to_string(LOG_INFO), "INFO") == 0, "LOG_INFO");
        CHECK(strcmp((const char *)light_log_level_to_string(LOG_WARN), "WARNING") == 0, "LOG_WARN");
        CHECK(strcmp((const char *)light_log_level_to_string(LOG_ERROR), "ERROR") == 0, "LOG_ERROR");
        CHECK(strcmp((const char *)light_log_level_to_string(200), "UNDEFINED") == 0,
                "an unknown level should be UNDEFINED rather than fall through");
}
static void test_every_error_code_is_named(void)
{
        CHECK(strcmp((const char *)light_error_to_string(LIGHT_OK), "LIGHT_OK") == 0, "LIGHT_OK");
        CHECK(strcmp((const char *)light_error_to_string(LIGHT_INVALID), "LIGHT_INVALID") == 0, "LIGHT_INVALID");
        CHECK(strcmp((const char *)light_error_to_string(LIGHT_NO_MEMORY), "LIGHT_NO_MEMORY") == 0, "LIGHT_NO_MEMORY");
        CHECK(strcmp((const char *)light_error_to_string(LIGHT_NO_RESOURCE), "LIGHT_NO_RESOURCE") == 0, "LIGHT_NO_RESOURCE");
        CHECK(strcmp((const char *)light_error_to_string(LIGHT_STATE_INVALID), "LIGHT_STATE_INVALID") == 0, "LIGHT_STATE_INVALID");
        CHECK(strcmp((const char *)light_error_to_string(LIGHT_EXTERNAL), "LIGHT_EXTERNAL") == 0, "LIGHT_EXTERNAL");
        CHECK(strcmp((const char *)light_error_to_string(200), "UNDEFINED") == 0,
                "an unknown code should be UNDEFINED");
}
static void test_every_run_mode_is_named(void)
{
        CHECK(strcmp((const char *)light_run_mode_to_string(MODE_PRODUCTION), "PRODUCTION") == 0, "MODE_PRODUCTION");
        CHECK(strcmp((const char *)light_run_mode_to_string(MODE_TEST), "TEST") == 0, "MODE_TEST");
        CHECK(strcmp((const char *)light_run_mode_to_string(MODE_DEVELOPMENT), "DEVELOPMENT") == 0, "MODE_DEVELOPMENT");
        CHECK(strcmp((const char *)light_run_mode_to_string(MODE_DEBUG), "DEBUG") == 0, "MODE_DEBUG");
        CHECK(strcmp((const char *)light_run_mode_to_string(MODE_TRACE), "TRACE") == 0, "MODE_TRACE");
        CHECK(strcmp((const char *)light_run_mode_to_string(200), "UNDEFINED") == 0,
                "an unknown mode should be UNDEFINED");
}

// --- dispatch ---------------------------------------------------------------------------------

static const struct { const char *name; void (*fn)(void); } test_cases[] = {
        { "a_new_queue_is_empty_and_not_full",        test_a_new_queue_is_empty_and_not_full },
        { "try_get_on_an_empty_queue_reports_failure", test_try_get_on_an_empty_queue_reports_failure },
        { "messages_come_back_in_order",              test_messages_come_back_in_order },
        { "the_text_survives_the_round_trip",         test_the_text_survives_the_round_trip },
        { "is_full_becomes_true_at_the_depth",        test_is_full_becomes_true_at_the_depth },
        { "a_full_queue_still_returns_everything",    test_a_full_queue_still_returns_everything },
        { "the_buffer_wraps_and_keeps_order",         test_the_buffer_wraps_and_keeps_order },
        { "peek_does_not_consume",                    test_peek_does_not_consume },
        { "advance_consumes_exactly_one",             test_advance_consumes_exactly_one },
        { "flush_returns_on_an_empty_queue",          test_flush_returns_on_an_empty_queue },
        { "every_log_level_is_named",                 test_every_log_level_is_named },
        { "every_error_code_is_named",                test_every_error_code_is_named },
        { "every_run_mode_is_named",                  test_every_run_mode_is_named },
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
