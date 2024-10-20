/*
 *  light_common.c
 *  common utility routines and data types for lightsource.nz projects
 * 
 *  authored by Alex Fulton
 *  created january 2023
 * 
 */

#include "light_common.h"

#include <stdio.h>
#include <string.h>

static uint8_t log_buffer[LIGHT_LOG_BUFFER_SIZE];

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
const uint8_t *light_error_to_string(uint8_t level)
{
        switch (level) {
        case LIGHT_OK:
                return "LIGHT_OK";
        case LIGHT_INVALID:
                return "LIGHT_INVALID";
        case LIGHT_NO_MEMORY:
                return "LIGHT_NO_MEMORY";
        case LIGHT_NO_RESOURCE:
                return "LIGHT_NO_RESOURCE";
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

void light_log_internal(const uint8_t level, const uint8_t *func, const uint8_t *format, ...)
{
        snprintf(log_buffer, LIGHT_LOG_BUFFER_SIZE, "[%7s] %s: ", light_log_level_to_string(level), func);
        printf(log_buffer);

        va_list args;
        va_start(args, format);
        vsnprintf(log_buffer, LIGHT_LOG_BUFFER_SIZE, format, args);
        va_end(args);
        printf(log_buffer);
        putc('\n', stdout);
}