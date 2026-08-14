/*
 *  test_light_log.c
 *  log message formatting, and what it does when the message will not fit
 *
 *  HOST ONLY: these run the real light_core and read back what it produced.
 *
 *  HOW THIS SEES THE OUTPUT. light_log_internal() takes the stream as its first argument, and
 *  on the fast path (LIGHT_MSG_FAST, which is 0, so a zeroed stream takes it whichever thread
 *  is logging) the fully formatted line is copied into that stream's message queue. So a test
 *  can log to a stream of its own and read the formatted text straight back out with
 *  light_stream_mqueue_try_get() -- no handler, no draining, no global state. The queue's
 *  per-message buffer is LIGHT_STREAM_MAX_MSG_LENGTH, the same 256 as the log buffer, so what
 *  comes back is exactly what the formatter produced rather than a second truncation of it.
 *
 *  TWO KINDS OF CASE HERE. The first group pins the observable contract -- the level and
 *  function prefix, argument expansion, and that an over-long message is cut to fit and still
 *  ends in a newline. The second group calls light_log_format() directly with guard bytes
 *  either side of the buffer, because output alone cannot show WHERE something was written:
 *  the formatter's last clamp yields a well-formed line even when an earlier step has already
 *  run off the end. There is no sanitizer available here (this toolchain has no libasan, and
 *  the conf-crush-debug-addrsan preset silently produces an unsanitised build), so guard bytes
 *  are the substitute.
 *
 *  BOTH GROUPS ARE MUTATION-CHECKED. Removing the reserved-newline clamp fails
 *  truncated_message_keeps_its_newline and long_function_name_is_bounded; removing the final
 *  clamp entirely -- a genuine one-byte overrun -- fails all three guard cases with "1 bytes
 *  outside the buffer were written".
 *
 *  ONE LIMIT WORTH KNOWING. Removing the clamp after the prefix snprintf is NOT detectable on
 *  this host. It leaves vsnprintf with a wrapped (enormous) size argument, and this C library
 *  rejects such a size and writes nothing -- verified directly. The clamp still belongs there,
 *  because that defence is the library's choice rather than the contract's, and newlib on the
 *  targets need not make the same one. It simply cannot be demonstrated from here.
 */
#include <light.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

//   light_core references `this_app`, so anything linking it has to define an application. It
// is never started -- main() calls the test functions directly
static void _test_app_event(const struct light_module *module, uint8_t event, void *arg) {}
static uint8_t _test_app_main(struct light_application *app) { return LF_STATUS_RUN; }
Light_Application_Define(test_light_log, _test_app_event, _test_app_main, &light_core);

static int failures;

#define CHECK(cond, ...) do { \
        if(!(cond)) { \
                failures++; \
                printf("  FAIL %s:%d: ", __func__, __LINE__); \
                printf(__VA_ARGS__); \
                printf("\n"); \
        } \
} while(0)

//   a stream of this test's own rather than light_stream_stdout: nothing else writes to it, so
// whatever comes out of the queue belongs to the case that just ran
static struct light_stream _stream;

static void _reset(void)
{
        memset(&_stream, 0, sizeof(_stream));
        light_mutex_init(&_stream.lock);
        light_stream_mqueue_init(&_stream.queue);
}

//   returns the formatted line the log path just queued, or NULL if it queued nothing. The
// storage is static so the caller can keep using it after this returns
static const char *_captured(void)
{
        static struct light_message message;

        if(!light_stream_mqueue_try_get(&_stream.lock, &_stream.queue, &message))
                return NULL;
        return (const char *)message.text;
}

// --- the shape of a formatted line ------------------------------------------------------------

//   "[  LEVEL] function: message\n" -- the level is right-aligned in a 7-character field, which
// is what lines the output up when levels of different lengths are interleaved
static void test_format_carries_level_and_function(void)
{
        _reset();
        light_log_internal(&_stream, LOG_INFO, (const uint8_t *)"myfunc", (const uint8_t *)"hello");

        const char *out = _captured();
        CHECK(out != NULL, "nothing was queued");
        if(!out)
                return;
        CHECK(strcmp(out, "[   INFO] myfunc: hello\n") == 0,
                        "formatted as '%s', expected '[   INFO] myfunc: hello\\n'", out);
}

static void test_format_expands_arguments(void)
{
        _reset();
        light_log_internal(&_stream, LOG_DEBUG, (const uint8_t *)"f",
                        (const uint8_t *)"n=%d s=%s", 42, "str");

        const char *out = _captured();
        CHECK(out != NULL, "nothing was queued");
        if(!out)
                return;
        CHECK(strstr(out, "n=42 s=str") != NULL,
                        "arguments did not expand: got '%s'", out);
}

// every line ends in exactly one newline, or the stream's consumer runs entries together
static void test_format_ends_with_one_newline(void)
{
        _reset();
        light_log_internal(&_stream, LOG_INFO, (const uint8_t *)"f", (const uint8_t *)"body");

        const char *out = _captured();
        CHECK(out != NULL, "nothing was queued");
        if(!out)
                return;
        size_t len = strlen(out);
        CHECK(len > 0 && out[len - 1] == '\n', "line does not end with a newline: '%s'", out);
        CHECK(len > 1 && out[len - 2] != '\n', "line ends with two newlines: '%s'", out);
}

//   the levels are distinguishable in the output. Worth pinning because the prefix is built
// with a %7s of light_log_level_to_string() -- a level whose name ran long would silently push
// the rest of the line out of alignment rather than fail
static void test_each_level_is_named(void)
{
        static const struct { uint8_t level; const char *name; } levels[] = {
                { LOG_TRACE, "TRACE" }, { LOG_DEBUG, "DEBUG" }, { LOG_INFO, "INFO" },
                { LOG_WARN, "WARNING" }, { LOG_ERROR, "ERROR" },
        };

        for(size_t i = 0; i < sizeof(levels) / sizeof(levels[0]); i++) {
                _reset();
                light_log_internal(&_stream, levels[i].level, (const uint8_t *)"f",
                                (const uint8_t *)"x");
                const char *out = _captured();
                CHECK(out != NULL, "nothing queued for level %s", levels[i].name);
                if(!out)
                        continue;
                CHECK(strstr(out, levels[i].name) != NULL,
                                "level %s missing from '%s'", levels[i].name, out);
                CHECK(out[0] == '[', "line does not open with '[': '%s'", out);
        }
}

// --- truncation -------------------------------------------------------------------------------

//   THE ONE THAT MATTERS. A message longer than the buffer must be cut to fit rather than
// written past the end. This is the case -Wstringop-overflow was pointing at: the old code
// passed a whole buffer SIZE to strncat as its bound, which is the number of characters it may
// ADD on top of what is already there
static void test_long_message_is_truncated_to_fit(void)
{
        char body[LIGHT_STREAM_MAX_MSG_LENGTH * 3];
        memset(body, 'x', sizeof(body) - 1);
        body[sizeof(body) - 1] = '\0';

        _reset();
        light_log_internal(&_stream, LOG_INFO, (const uint8_t *)"f", (const uint8_t *)"%s", body);

        const char *out = _captured();
        CHECK(out != NULL, "nothing was queued");
        if(!out)
                return;
        size_t len = strlen(out);
        CHECK(len < LIGHT_STREAM_MAX_MSG_LENGTH,
                        "formatted length %zu is not below the %d-byte limit",
                        len, LIGHT_STREAM_MAX_MSG_LENGTH);
        CHECK(strncmp(out, "[   INFO] f: ", 13) == 0,
                        "the prefix did not survive truncation: '%.20s'", out);
}

//   a truncated line keeps its newline. The formatter reserves room for it rather than
// appending only if space happens to remain -- an over-long entry that lost its line ending
// would run straight into the next one, which is a corrupted log rather than a shortened one
static void test_truncated_message_keeps_its_newline(void)
{
        char body[LIGHT_STREAM_MAX_MSG_LENGTH * 3];
        memset(body, 'y', sizeof(body) - 1);
        body[sizeof(body) - 1] = '\0';

        _reset();
        light_log_internal(&_stream, LOG_INFO, (const uint8_t *)"f", (const uint8_t *)"%s", body);

        const char *out = _captured();
        CHECK(out != NULL, "nothing was queued");
        if(!out)
                return;
        size_t len = strlen(out);
        CHECK(len > 0 && out[len - 1] == '\n',
                        "a truncated line lost its newline (length %zu)", len);
}

//   the prefix alone can overrun the buffer, before any of the message is written. __func__ is
// normally short, but nothing enforces that, and the formatter has to clamp what snprintf
// reports -- snprintf returns what it WANTED to write, not what it managed to
static void test_long_function_name_is_bounded(void)
{
        char func[LIGHT_STREAM_MAX_MSG_LENGTH * 2];
        memset(func, 'f', sizeof(func) - 1);
        func[sizeof(func) - 1] = '\0';

        _reset();
        light_log_internal(&_stream, LOG_ERROR, (const uint8_t *)func, (const uint8_t *)"tail");

        const char *out = _captured();
        CHECK(out != NULL, "nothing was queued");
        if(!out)
                return;
        size_t len = strlen(out);
        CHECK(len < LIGHT_STREAM_MAX_MSG_LENGTH,
                        "formatted length %zu is not below the %d-byte limit",
                        len, LIGHT_STREAM_MAX_MSG_LENGTH);
        CHECK(len > 0 && out[len - 1] == '\n',
                        "line with an over-long function name lost its newline");
}

// an empty message body still produces a well-formed, newline-terminated line
static void test_empty_message_is_well_formed(void)
{
        _reset();
        light_log_internal(&_stream, LOG_INFO, (const uint8_t *)"f", (const uint8_t *)"");

        const char *out = _captured();
        CHECK(out != NULL, "nothing was queued");
        if(!out)
                return;
        CHECK(strcmp(out, "[   INFO] f: \n") == 0,
                        "formatted as '%s', expected '[   INFO] f: \\n'", out);
}

// --- staying inside the buffer ----------------------------------------------------------------

//   the cases above go through light_log_internal() and read the queue, which shows what was
// formatted but NOT where it was written. Those two differ: light_log_format() clamps its
// length one last time before appending the newline, so a step that has already run off the end
// still yields a well-formed line. An out-of-bounds write is invisible from the output.
//
//   this was not hypothetical -- removing the clamp after the prefix snprintf leaves
// vsnprintf() writing at out + 520 in a 256-byte buffer, and every one of the cases above still
// passed. So these call the formatter directly with guard bytes either side and check they
// survive, which is the closest thing available to a sanitizer on this toolchain.
#define GUARD_SIZE      64
#define GUARD_BYTE      0xAA
//   the arena has to be big enough that a plausible overrun lands INSIDE it. This is not
// padding for its own sake: an unclamped snprintf result becomes the offset vsnprintf writes
// at, so a 2048-character function name puts the write ~2060 bytes into the buffer. A first
// version of this used a 1024-byte arena, the write sailed clean over the far guard, and the
// mutation went undetected -- a guard you can jump over is not a guard
#define ARENA_BODY      8192

//   returns the number of bytes outside the region the formatter was given that it wrote to.
// `body_size` is what it is TOLD it has, deliberately far smaller than the allocation, so an
// overrun has somewhere to land and be seen rather than corrupting the test's own frame
static int _format_and_check_guards(size_t body_size, const uint8_t *func,
                                        const uint8_t *format, ...)
{
        static uint8_t arena[GUARD_SIZE + ARENA_BODY + GUARD_SIZE];
        uint8_t *body = arena + GUARD_SIZE;
        va_list args;
        int disturbed = 0;

        memset(arena, GUARD_BYTE, sizeof(arena));
        va_start(args, format);
        light_log_format(body, body_size, LOG_INFO, func, format, args);
        va_end(args);

        for(size_t i = 0; i < GUARD_SIZE; i++) {
                if(arena[i] != GUARD_BYTE)
                        disturbed++;
        }
        // everything past the size the formatter was given, up to the end of the arena
        for(size_t i = GUARD_SIZE + body_size; i < sizeof(arena); i++) {
                if(arena[i] != GUARD_BYTE)
                        disturbed++;
        }
        return disturbed;
}

static void test_stays_within_bounds_for_a_normal_message(void)
{
        int disturbed = _format_and_check_guards(256, (const uint8_t *)"func",
                                (const uint8_t *)"a short message");
        CHECK(disturbed == 0, "%d bytes outside the buffer were written", disturbed);
}

//   an over-long BODY. vsnprintf is given the remaining space and must not exceed it, and the
// newline and terminator must land inside too
static void test_stays_within_bounds_for_a_long_message(void)
{
        char body[2048];
        memset(body, 'x', sizeof(body) - 1);
        body[sizeof(body) - 1] = '\0';

        int disturbed = _format_and_check_guards(256, (const uint8_t *)"func",
                                (const uint8_t *)"%s", body);
        CHECK(disturbed == 0, "%d bytes outside the buffer were written", disturbed);
}

//   an over-long PREFIX, which is the case the output cannot show. snprintf reports what it
// wanted to write, so an unclamped result becomes an offset past the end and everything after
// it lands outside the buffer
static void test_stays_within_bounds_for_a_long_function_name(void)
{
        char func[2048];
        memset(func, 'f', sizeof(func) - 1);
        func[sizeof(func) - 1] = '\0';

        int disturbed = _format_and_check_guards(256, (const uint8_t *)func,
                                (const uint8_t *)"tail");
        CHECK(disturbed == 0, "%d bytes outside the buffer were written", disturbed);
}

// the smallest buffers the formatter accepts at all, where every clamp is at its boundary
static void test_stays_within_bounds_for_tiny_buffers(void)
{
        for(size_t size = 2; size <= 24; size++) {
                int disturbed = _format_and_check_guards(size, (const uint8_t *)"function",
                                        (const uint8_t *)"message body");
                CHECK(disturbed == 0, "%d bytes written outside a %zu-byte buffer",
                                disturbed, size);
                if(disturbed)
                        return;
        }
}

// --- harness -----------------------------------------------------------------------------------

struct test_case {
        const char *name;
        void (*fn)(void);
};
static const struct test_case test_cases[] = {
        { "format_carries_level_and_function", test_format_carries_level_and_function },
        { "format_expands_arguments",          test_format_expands_arguments },
        { "format_ends_with_one_newline",      test_format_ends_with_one_newline },
        { "each_level_is_named",               test_each_level_is_named },
        { "long_message_is_truncated_to_fit",  test_long_message_is_truncated_to_fit },
        { "truncated_message_keeps_its_newline", test_truncated_message_keeps_its_newline },
        { "long_function_name_is_bounded",     test_long_function_name_is_bounded },
        { "empty_message_is_well_formed",      test_empty_message_is_well_formed },
        { "stays_within_bounds_for_a_normal_message", test_stays_within_bounds_for_a_normal_message },
        { "stays_within_bounds_for_a_long_message", test_stays_within_bounds_for_a_long_message },
        { "stays_within_bounds_for_a_long_function_name", test_stays_within_bounds_for_a_long_function_name },
        { "stays_within_bounds_for_tiny_buffers", test_stays_within_bounds_for_tiny_buffers },
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
