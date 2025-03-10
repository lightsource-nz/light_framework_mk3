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
                _log_synchronous(stream, level, func, format, args);
        } else switch(light_stream_get_background_logging_mode(stream)) {
                case LIGHT_MSG_FAST:
                _log_fast(stream, level, func, format, args);
                break;
                case LIGHT_MSG_FASTER:
                _log_faster(stream, level, func, format, args);
                break;
        }
}
static void _log_synchronous(struct light_stream *stream, const uint8_t level, const uint8_t *func, const uint8_t *format, va_list args)
{
        uint8_t log_buffer_pri[LIGHT_LOG_BUFFER_PRI_SIZE];
        uint8_t log_buffer_sec[LIGHT_LOG_BUFFER_SEC_SIZE];
        uint8_t *restrict cursor;
        memset(&log_buffer_pri, 0, LIGHT_LOG_BUFFER_PRI_SIZE);
        memset(&log_buffer_sec, 0, LIGHT_LOG_BUFFER_SEC_SIZE);
        uint8_t *message;
        uint8_t max_copy = (LIGHT_LOG_BUFFER_PRI_SIZE < LIGHT_LOG_BUFFER_SEC_SIZE)? LIGHT_LOG_BUFFER_PRI_SIZE : LIGHT_LOG_BUFFER_SEC_SIZE;
        snprintf(log_buffer_pri, LIGHT_LOG_BUFFER_PRI_SIZE, "[%7s] %s: ", light_log_level_to_string(level), func);
        cursor = strncat(log_buffer_sec, log_buffer_pri, max_copy);
       
        vsnprintf(log_buffer_pri, LIGHT_LOG_BUFFER_SEC_SIZE - (cursor - log_buffer_sec), format, args);
        cursor = strncat(cursor, log_buffer_pri, max_copy);
        cursor = strcat(cursor, "\n");
        stream->handler(stream, log_buffer_sec);
}
static void _log_fast(struct light_stream *stream, const uint8_t level, const uint8_t *func, const uint8_t *format, va_list args)
{
        uint8_t log_buffer_pri[LIGHT_LOG_BUFFER_PRI_SIZE];
        uint8_t log_buffer_sec[LIGHT_LOG_BUFFER_SEC_SIZE];
        uint8_t *restrict cursor;
        uint8_t max_copy = (LIGHT_LOG_BUFFER_PRI_SIZE < LIGHT_LOG_BUFFER_SEC_SIZE)? LIGHT_LOG_BUFFER_SEC_SIZE : LIGHT_LOG_BUFFER_PRI_SIZE;
        snprintf(log_buffer_pri, LIGHT_LOG_BUFFER_PRI_SIZE, "[%7s] %s: ", light_log_level_to_string(level), func);
        cursor = strncat(log_buffer_sec, log_buffer_pri, max_copy);
       
        vsnprintf(log_buffer_pri, LIGHT_LOG_BUFFER_SEC_SIZE - (cursor - log_buffer_sec), format, args);
        cursor = strncat(cursor, log_buffer_pri, max_copy);
        cursor = strcat(cursor, "\n");
        uint8_t *msg_text = light_alloc(strlen(log_buffer_sec));
        strcpy(msg_text, log_buffer_sec);
        light_stream_mqueue_add_fast(&stream->queue, msg_text);
}
static void _log_faster(struct light_stream *stream, const uint8_t level, const uint8_t *func, const uint8_t *format, va_list args)
{
        light_stream_message_vf_faster(stream, format, args);
}