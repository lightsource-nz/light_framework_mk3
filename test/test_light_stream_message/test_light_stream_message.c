// checks for light_stream's message API -- the producer half of the logging transport.
//
// SEPARATE FROM test_light_stream.c, which drives a bare light_stream_mqueue directly. That
// covers the circular buffer; this covers what light_info()/light_warn()/light_error()
// actually call into, which coverage showed was untested end to end: every
// light_stream_message_* entry point and mqueue_put_formatted() sat at 0%.
//
// WHY IT NEVER CALLS light_stream_setup(). On a host build that spawns a worker thread which
// drains the queues in the background, and half the assertions here are about what has NOT
// been written yet -- a race that would make this suite flaky rather than wrong. Instead a
// local stream is brought up with light_stream_init() (which is what setup() calls per stream
// anyway) and drained by hand. That is also what keeps the suite honest about ordering: the
// drain happens exactly where the test says it does.
//
// RUN AS: ctest, or this binary directly. With no argument it runs everything; with a case
// name it runs just that one, which is how CTest registers them individually.
#include <light.h>
#include <light_stream.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// see the other suites -- a binary linking light_core has to be an application so that
// framework.c's reference to `this_app` resolves. It is never started
static void _test_app_event(const struct light_module *module, uint8_t event, void *arg) {}
static uint8_t _test_app_main(struct light_application *app) { return LF_STATUS_RUN; }
Light_Application_Define(test_light_stream_message, _test_app_event, _test_app_main, &light_core);

static int failures;

#define CHECK(cond, ...) do { \
        if(!(cond)) { \
                printf("FAIL %s:%d: ", __FILE__, __LINE__); \
                printf(__VA_ARGS__); printf("\n"); \
                failures++; \
        } \
} while(0)

// ---------------------------------------------------------------------------------------------
//   the capture stream. Its handlers record what they were given instead of writing it, which
// is the only way to see the difference between "formatted now and queued" and "queued now and
// formatted later" -- on stdout both look identical, and only one of them is correct
// ---------------------------------------------------------------------------------------------
#define CAPTURE_MAX 8
static struct {
        int calls;
        char text[CAPTURE_MAX][LIGHT_STREAM_MAX_MSG_LENGTH];
} cap;

static void _capture(const char *s)
{
        if(cap.calls < CAPTURE_MAX)
                snprintf(cap.text[cap.calls], LIGHT_STREAM_MAX_MSG_LENGTH, "%s", s);
        cap.calls++;
}
static int _handler(struct light_stream *stream, const char *restrict message)
{
        _capture(message);
        return 0;
}
static int _handler_va(struct light_stream *stream, const char *restrict format, ...)
{
        //   deliberately the SAME SHAPE as msg_stdout_v(): va_start over its own varargs, then
        // hand that to a v*printf. Not a tidier signature taking a va_list directly, because
        // this entry point is called both ways -- light_stream_init() calls it as an ordinary
        // variadic, light_stream_message_vf_sync() passes a va_list through it -- and a test
        // handler that only worked for one of those would be testing a calling convention the
        // real streams do not have
        va_list args;
        va_start(args, format);
        char buf[LIGHT_STREAM_MAX_MSG_LENGTH];
        vsnprintf(buf, sizeof(buf), format, args);
        va_end(args);
        _capture(buf);
        return 0;
}

static struct light_stream test_stream =
        Light_Stream("test_stream", LIGHT_MSG_FAST, _handler, _handler_va);

static struct light_stream *setup(void)
{
        memset(&cap, 0, sizeof(cap));
        light_core_impl_setup();
        light_stream_init(&test_stream);
        //   light_stream_init() writes an "opening message stream" line through the handler
        // before the queue exists, so the counters are cleared AFTER it rather than before
        memset(&cap, 0, sizeof(cap));
        return &test_stream;
}
// one message out of the queue and into the handler, the way the real drain does it
static bool drain_one(struct light_stream *stream)
{
        struct light_stream_mqueue *q = light_stream_get_queue(stream);
        if(light_stream_mqueue_is_empty(q))
                return false;
        struct light_message *msg = light_stream_mqueue_peek(q);
        _capture((const char *)msg->text);
        light_stream_mqueue_advance(q);
        return true;
}
//   light_stream_message_vf_faster() takes a va_list, so reaching it at all needs a variadic
// caller to build one. Firmware code gets there through a logging macro; this stands in for it
static void call_vf_faster(struct light_stream *stream, const uint8_t *format, ...)
{
        va_list args;
        va_start(args, format);
        light_stream_message_vf_faster(stream, format, args);
        va_end(args);
}
static uint8_t queued_count(struct light_stream *stream)
{
        return light_stream_mqueue_is_empty(light_stream_get_queue(stream)) ? 0 : 1;
}

// --- 1. the synchronous path writes through, without touching the queue ---
static void test_sync_reaches_the_handler_immediately(void)
{
        struct light_stream *s = setup();

        light_stream_message_f_sync(s, (const uint8_t *)"value is %d", 42);
        CHECK(cap.calls == 1, "expected one handler call, got %d", cap.calls);
        CHECK(strcmp(cap.text[0], "value is 42") == 0,
              "handler got '%s', expected 'value is 42'", cap.text[0]);
        //   nothing queued: sync means the caller has paid for the write by the time it
        // returns. That is the entire distinction from the fast path, and it is what
        // light_fatal() depends on to get its message out before the process ends
        CHECK(queued_count(s) == 0, "the sync path should not have queued anything");

        // and the non-variadic wrapper takes the same route
        light_stream_message_sync(s, (const uint8_t *)"plain");
        CHECK(cap.calls == 2 && strcmp(cap.text[1], "plain") == 0,
              "message_sync got '%s' after %d calls", cap.text[1], cap.calls);
}

// --- 2. the fast path defers the WRITE but not the formatting ---
static void test_fast_queues_rather_than_writing(void)
{
        struct light_stream *s = setup();

        light_stream_message_f_fast(s, (const uint8_t *)"queued %d", 7);
        CHECK(cap.calls == 0, "the fast path wrote through instead of queueing (%d calls)",
              cap.calls);
        CHECK(queued_count(s) == 1, "the fast path did not queue the message");

        CHECK(drain_one(s), "expected a message in the queue");
        CHECK(strcmp(cap.text[0], "queued 7") == 0,
              "drained '%s', expected 'queued 7'", cap.text[0]);
        CHECK(queued_count(s) == 0, "the queue should be empty after draining one message");

        //   the non-variadic wrapper takes the same route. Worth its own line because it
        // forwards a caller-supplied string as the FORMAT argument, so any '%' in a logged
        // message is a conversion -- the same trap msg_stdout() carries a comment about
        light_stream_message_fast(s, (const uint8_t *)"plain text");
        CHECK(queued_count(s) == 1, "message_fast did not queue");
        CHECK(drain_one(s) && strcmp(cap.text[1], "plain text") == 0,
              "message_fast queued '%s'", cap.text[1]);
}

// --- 3. THE property that makes the fast path safe ---
static void test_formatting_happens_at_call_time(void)
{
        struct light_stream *s = setup();

        //   the argument is mutated between queueing and draining. If the queue held the
        // format string and its arguments rather than the finished text, this is where it
        // would go wrong -- and in the firmware the "argument" is usually a stack local that
        // has gone out of scope entirely by the time a worker thread drains it, which does not
        // fail loudly, it just logs whatever is on that stack now
        char subject[32];
        snprintf(subject, sizeof(subject), "before");
        light_stream_message_f_fast(s, (const uint8_t *)"subject=%s n=%d", subject, 1);
        snprintf(subject, sizeof(subject), "AFTER!");

        CHECK(drain_one(s), "expected a message in the queue");
        CHECK(strcmp(cap.text[0], "subject=before n=1") == 0,
              "drained '%s' -- expected 'subject=before n=1', so formatting was deferred",
              cap.text[0]);
}

// --- 4. an over-long message is truncated, not overrun ---
static void test_a_long_message_is_truncated(void)
{
        struct light_stream *s = setup();

        //   the queue slot is a fixed LIGHT_STREAM_MAX_MSG_LENGTH array inside the stream, so
        // an unbounded write here does not fail at the call site -- it walks into the next
        // slot, or past the end of the queue into the stream's own fields
        char big[LIGHT_STREAM_MAX_MSG_LENGTH * 2];
        memset(big, 'x', sizeof(big) - 1);
        big[sizeof(big) - 1] = '\0';

        light_stream_message_f_fast(s, (const uint8_t *)"%s", big);
        struct light_message *msg = light_stream_mqueue_peek(light_stream_get_queue(s));
        size_t len = strnlen((const char *)msg->text, LIGHT_STREAM_MAX_MSG_LENGTH);
        CHECK(len < LIGHT_STREAM_MAX_MSG_LENGTH,
              "a %zu-byte message filled the whole %d-byte slot with no terminator",
              sizeof(big), LIGHT_STREAM_MAX_MSG_LENGTH);
        CHECK(msg->text[len] == '\0', "the truncated message is not NUL-terminated");
}

// --- 5. the queue survives more messages than one, in order ---
static void test_queued_messages_keep_their_order(void)
{
        struct light_stream *s = setup();

        for(int i = 0; i < 4; i++)
                light_stream_message_f_fast(s, (const uint8_t *)"msg %d", i);
        CHECK(cap.calls == 0, "queueing wrote through (%d calls)", cap.calls);

        for(int i = 0; i < 4; i++) {
                CHECK(drain_one(s), "expected message %d still queued", i);
                char want[32];
                snprintf(want, sizeof(want), "msg %d", i);
                CHECK(strcmp(cap.text[i], want) == 0,
                      "drained '%s' at position %d, expected '%s'", cap.text[i], i, want);
        }
}

// --- 6. each entry point stamps the flags it says it does ---
static void test_messages_carry_their_flags(void)
{
        struct light_stream *s = setup();

        light_stream_message_f_fast(s, (const uint8_t *)"fast");
        struct light_message *msg = light_stream_mqueue_peek(light_stream_get_queue(s));
        CHECK(msg->flags == LIGHT_MSG_FAST, "the fast path stamped flags=%d, expected %d",
              msg->flags, LIGHT_MSG_FAST);
        light_stream_mqueue_advance(light_stream_get_queue(s));

        //   NOTE the asymmetry this pins down: light_stream_message_f_faster() forwards to
        // light_stream_message_vf_FAST(), so it stamps LIGHT_MSG_FAST, while calling
        // light_stream_message_vf_faster() directly stamps LIGHT_MSG_FASTER. Both format
        // eagerly -- stream.c says so at the site -- so the flag is the only difference, and
        // this records which entry point produces which rather than asserting they agree
        light_stream_message_f_faster(s, (const uint8_t *)"faster");
        msg = light_stream_mqueue_peek(light_stream_get_queue(s));
        CHECK(msg->flags == LIGHT_MSG_FAST,
              "f_faster stamped flags=%d; it forwards to vf_fast, so %d is expected here",
              msg->flags, LIGHT_MSG_FAST);
        CHECK(strcmp((const char *)msg->text, "faster") == 0,
              "f_faster queued '%s'", (const char *)msg->text);
        light_stream_mqueue_advance(light_stream_get_queue(s));

        // ...and reaching vf_faster directly gives the other flag, which is the asymmetry
        call_vf_faster(s, (const uint8_t *)"direct %d", 9);
        msg = light_stream_mqueue_peek(light_stream_get_queue(s));
        CHECK(msg->flags == LIGHT_MSG_FASTER,
              "vf_faster stamped flags=%d, expected %d", msg->flags, LIGHT_MSG_FASTER);
        CHECK(strcmp((const char *)msg->text, "direct 9") == 0,
              "vf_faster queued '%s', expected 'direct 9'", (const char *)msg->text);
}

// --- 7. the background logging mode is per stream and round-trips ---
static void test_background_logging_mode_round_trips(void)
{
        struct light_stream *s = setup();

        light_stream_set_background_logging_mode(s, LIGHT_MSG_FASTER);
        CHECK(light_stream_get_background_logging_mode(s) == LIGHT_MSG_FASTER,
              "set FASTER, read back %d", light_stream_get_background_logging_mode(s));

        light_stream_set_background_logging_mode(s, LIGHT_MSG_FAST);
        CHECK(light_stream_get_background_logging_mode(s) == LIGHT_MSG_FAST,
              "set FAST, read back %d", light_stream_get_background_logging_mode(s));
}

// --- 8. the output lock is held and released, not leaked ---
static void test_output_lock_round_trips(void)
{
        struct light_stream *s = setup();

        //   a lock that is never released deadlocks the next writer, and on a single-threaded
        // test that is a hang rather than a failure -- so this case existing at all depends on
        // the unlock working. It runs twice for that reason: the second acquire is what proves
        // the first release happened
        light_stream_lock_output(s);
        light_stream_unlock_output(s);
        light_stream_lock_output(s);
        light_stream_unlock_output(s);

        light_stream_message_f_sync(s, (const uint8_t *)"after the lock");
        CHECK(cap.calls == 1 && strcmp(cap.text[0], "after the lock") == 0,
              "the stream was unusable after locking and unlocking it");
}

static const struct { const char *name; void (*fn)(void); } test_cases[] = {
        { "sync_reaches_the_handler_immediately", test_sync_reaches_the_handler_immediately },
        { "fast_queues_rather_than_writing",      test_fast_queues_rather_than_writing },
        { "formatting_happens_at_call_time",      test_formatting_happens_at_call_time },
        { "a_long_message_is_truncated",          test_a_long_message_is_truncated },
        { "queued_messages_keep_their_order",     test_queued_messages_keep_their_order },
        { "messages_carry_their_flags",           test_messages_carry_their_flags },
        { "background_logging_mode_round_trips",  test_background_logging_mode_round_trips },
        { "output_lock_round_trips",              test_output_lock_round_trips },
};
#define TEST_CASE_COUNT (sizeof(test_cases) / sizeof(*test_cases))

int main(int argc, char **argv)
{
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
                // an unknown name must be an error, not a silent pass -- a typo in
                // CMakeLists.txt would otherwise register a test that always succeeds
                printf("FAIL: no such test case '%s'\n", argv[1]);
                return 2;
        }

        for(size_t i = 0; i < TEST_CASE_COUNT; i++)
                test_cases[i].fn();
        printf("%s: %d failure(s)\n", failures ? "FAILED" : "PASSED", failures);
        return failures ? 1 : 0;
}
