/*
 *  light_common.c
 *  common utility routines and data types for lightsource.nz projects
 * 
 *  authored by Alex Fulton
 *  created january 2023
 * 
 */

#include <stdarg.h>
#include <stdint.h>
#include <stdbool.h>
#include <light_core_port.h>
#include <light_object.h>
#include <light_stream.h>
#include <light_common.h>
#include <light_platform.h>

#include <stdio.h>
#include <string.h>

void light_common_init()
{

}
void *light_alloc(size_t size)
{
        return malloc(size);
}
void light_free(void *obj)
{
        free(obj);
}
#define def_case(err_code) case err_code:; return #err_code
const uint8_t *light_error_to_string(uint8_t level)
{
        switch (level) {
                def_case(LIGHT_OK);
                def_case(LIGHT_INVALID);
                def_case(LIGHT_NO_MEMORY);
                def_case(LIGHT_NO_RESOURCE);
                def_case(LIGHT_STATE_INVALID);
                def_case(LIGHT_EXTERNAL);
        default:
                return "UNDEFINED";
        }
}
const uint8_t *light_run_mode_to_string(uint8_t level)
{
        switch (level) {
        case MODE_PRODUCTION:
                return "PRODUCTION";
        case MODE_TEST:
                return "TEST";
        case MODE_DEVELOPMENT:
                return "DEVELOPMENT";
        case MODE_DEBUG:
                return "DEBUG";
        case MODE_TRACE:
                return "TRACE";
        default:
                return "UNDEFINED";
        }
}
const uint8_t *light_log_level_to_string(uint8_t level)
{
        switch (level) {
        case LOG_TRACE:
                return "TRACE";
        case LOG_DEBUG:
                return "DEBUG";
        case LOG_INFO:
                return "INFO";
        case LOG_WARN:
                return "WARNING";
        case LOG_ERROR:
                return "ERROR";
        default:
                return "UNDEFINED";
        }
}
static void _log_synchronous(struct light_stream *stream, const uint8_t level, const uint8_t *func, const uint8_t *format, va_list args);
static void _log_fast(struct light_stream *stream, const uint8_t level, const uint8_t *func, const uint8_t *format, va_list args);
static void _log_faster(struct light_stream *stream, const uint8_t level, const uint8_t *func, const uint8_t *format, va_list args);
void light_log_internal(struct light_stream *stream, const uint8_t level, const uint8_t *func, const uint8_t *format, ...)
{
        va_list args;
        va_start(args, format);
        if(light_platform_task_is_main()) {
                //   TODO add options controlling main-thread logging policy.
                // default behaviour sends all log messages to the same background queue,
                // ensuring the best preservation of time-ordering in the output log
                
                // _log_synchronous(stream, level, func, format, args);

                _log_fast(stream, level, func, format, args);
        } else switch(light_stream_get_background_logging_mode(stream)) {
                case LIGHT_MSG_FAST:
                _log_fast(stream, level, func, format, args);
                break;
                case LIGHT_MSG_FASTER:
                _log_faster(stream, level, func, format, args);
                break;
        }
}
//   formats "[  LEVEL] func: message\n" into `out`, returning the length written. Shared by the
// synchronous and queued log paths, which were near-identical copies of this and carried the
// same three bounds bugs between them:
//
//   - the bound handed to strncat() was a whole BUFFER SIZE. strncat's third argument is how
//     many characters it may add on top of what the destination already holds, and it writes a
//     terminator after them -- so passing 256 into a 256-byte buffer that already held the
//     prefix could write past the end. That is what -Wstringop-overflow reported.
//   - the return of strncat() was assigned to a `cursor` in the belief that it pointed past the
//     text. strncat returns the DESTINATION, so cursor never advanced, and every "space
//     remaining" expression derived from it evaluated to the entire buffer.
//   - the trailing strcat(cursor, "\n") was unbounded.
//
//   the two copies had also drifted: _log_fast picked the LARGER of the two buffer sizes where
// _log_synchronous picked the smaller, an inversion invisible only because both are 256.
//
//   writing straight into the destination also retires the second 256-byte scratch buffer each
// of them kept on the stack, which is worth having back on a target
static size_t _log_format(uint8_t *out, size_t size, const uint8_t level, const uint8_t *func,
                                const uint8_t *format, va_list args)
{
        if(size < 2)
                return 0;

        //   snprintf reports what it WANTED to write, which may exceed the buffer, so every
        // result is clamped to what actually landed before it is used as an offset
        int written = snprintf(out, size, "[%7s] %s: ", light_log_level_to_string(level), func);
        size_t used = (written < 0) ? 0 : (size_t) written;
        if(used > size - 1)
                used = size - 1;

        written = vsnprintf(out + used, size - used, format, args);
        if(written > 0) {
                used += (size_t) written;
                if(used > size - 1)
                        used = size - 1;
        }

        //   room is always kept for the newline and the terminator, so an over-long message
        // gives up its last character rather than its line ending -- a truncated entry that
        // lost the '\n' would run straight into the next one. size >= 2 is checked above, so
        // size - 2 cannot underflow
        if(used > size - 2)
                used = size - 2;
        out[used++] = '\n';
        out[used] = '\0';
        return used;
}
static void _log_synchronous(struct light_stream *stream, const uint8_t level, const uint8_t *func, const uint8_t *format, va_list args)
{
        uint8_t log_buffer[LIGHT_LOG_BUFFER_SEC_SIZE];

        _log_format(log_buffer, sizeof(log_buffer), level, func, format, args);
        stream->handler(stream, log_buffer);
}
static void _log_fast(struct light_stream *stream, const uint8_t level, const uint8_t *func, const uint8_t *format, va_list args)
{
        uint8_t log_buffer[LIGHT_LOG_BUFFER_SEC_SIZE];

        _log_format(log_buffer, sizeof(log_buffer), level, func, format, args);
        // light_stream_mqueue_add_fast() copies this into the queue slot itself -- no
        // heap allocation needed here (log_buffer is a local stack buffer)
        light_stream_mqueue_add_fast(&stream->lock, &stream->queue, log_buffer);
}
static void _log_faster(struct light_stream *stream, const uint8_t level, const uint8_t *func, const uint8_t *format, va_list args)
{
        light_stream_message_vf_faster(stream, format, args);
}