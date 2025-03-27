/*
 *  light_cli/src/message.c
 *  functions implementing the light_cli console message API
 * 
 *  authored by Alex Fulton
 *  created november 2024
 * 
 */
#ifdef __GNUC__
#define _GNU_SOURCE
#endif
#include <light.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#ifndef _STDC_NO_THREADS_
#include <threads.h>
#endif

#define LTYPE_LIGHT_STREAM_NAME           "light_stream"
static void _stream_release(struct light_object *obj);
static void _stream_event_add(struct light_object *obj, struct light_object *child);
struct lobj_type ltype_light_stream = {
        .id = LTYPE_LIGHT_STREAM_NAME,
        .release = _stream_release,
        .evt_add = _stream_event_add,
        .evt_child_add = NULL,
        .evt_child_remove = NULL
};
static void _stream_release(struct light_object *obj)
{
        light_free(to_stream(obj));
}
static void _stream_event_add(struct light_object *obj, struct light_object *child)
{

}
static int msg_stdout(struct light_stream *stream, const char *restrict message)
{
        printf(message);
}
static int msg_stdout_va(struct light_stream *stream, const char *restrict format, va_list args)
{
        vprintf(format, args);
}
static int msg_stdout_v(struct light_stream *stream, const char *restrict format, ...)
{
        va_list args;
        va_start(args, format);
        msg_stdout_va(stream, format, args);
        va_end(args);
}
static int msg_stderr(struct light_stream *stream, const char *restrict message)
{
// TODO add this macro to light_platform
#if (LIGHT_PLATFORM_HAS_STDERR)
        fprintf(stderr, message);
#else
        printf(message);
#endif
}
static int msg_stderr_va(struct light_stream *stream, const char *restrict format, va_list args)
{
#if (LIGHT_PLATFORM_HAS_STDERR)
        vfprintf(stderr, format, args);
#else
        vprintf(format, args);
#endif
}
static int msg_stderr_v(struct light_stream *stream, const char *restrict format, ...)
{
        va_list args;
        va_start(args, format);
        msg_stderr_va(stream, format, args);
        va_end(args);
}

extern int __light_streams_start, __light_streams_end;

static struct light_stream **static_streams;
static uintptr_t static_stream_count;

// -> by default the standard output streams perform message formatting on the calling thread,
// then defer writing to the actual underlying output stream to the background I/O thread
Light_Stream_Define(light_stream_stdout, LIGHT_MSG_FAST, msg_stdout, msg_stdout_v);
Light_Stream_Define(light_stream_stderr, LIGHT_MSG_FAST, msg_stderr, msg_stderr_v);

static uint8_t streams_defined_count;
static struct light_stream *streams_defined[LIGHT_STREAM_MAX_STREAMS];
static thrd_t worker_thread;

static void _find_static_streams()
{
        printf("&__light_streams_start=0x%x, &__light_streams_end=0x%x, sizeof(void *)=0x%x\n", &__light_streams_start, &__light_streams_end, sizeof(void *));
        printf("((_start - _end = 0x%x) / 0x%x)=0x%x\n",((uintptr_t)&__light_streams_end) - (uintptr_t)&__light_streams_start, sizeof(void *), (((uintptr_t)&__light_streams_end) - ((uintptr_t)&__light_streams_start)) / sizeof(void *));
        static_streams = (struct light_stream **) &__light_streams_start;
        static_stream_count = (((uintptr_t)&__light_streams_end) - ((uintptr_t)&__light_streams_start)) / sizeof(void *);
        printf("located %d static output streams\n", static_stream_count);
}
static void _load_static_streams()
{
        for(uint16_t i = 0; i < static_stream_count; i++) {
                if(streams_defined_count >= LIGHT_STREAM_MAX_STREAMS)
                        break;
                light_stream_init(static_streams[i]);
        }
        printf("loaded %d static output streams\n", static_stream_count);
}
static int worker__handle_background_message_streams(void *arg);
void light_stream_setup()
{
        _find_static_streams();
        streams_defined_count = 0;
        _load_static_streams();
#ifdef LIGHT_PLATFORM_HAS_C11_THREADS
        if(0 != thrd_create(&worker_thread, worker__handle_background_message_streams, NULL)) {
                light_fatal("failed to launch background messaging worker thread");
        }
        light_debug("background messaging worker launched");
#endif
}
void light_stream_shutdown()
{
        // TODO implement wakeup and shutdown signals for worker thread so it can be terminated hered
}

static int worker__handle_background_message_streams(void *arg) {
        while(1) {
                light_stream_service_message_queues();
        }
}
// -> void light_stream_service_message_queues():
// -> this routine iterates once over the list of active message streams, processing at most one
// message from each queue before returning. to exhaustively process all incoming messages, this
// routine should be called repeatedly. however, if new messages continue arriving, there is no
// guarantee that the service routine will be able to keep up.
//   TODO this routine should sleep when all queues are idle, and use a wake-up signal that is
// triggered by new messages arriving on any queue, rather than busy-waiting on empty queues
void light_stream_service_message_queues()
{
        for(uint8_t i = 0; i < streams_defined_count; i++) {
                struct light_stream *stream = streams_defined[i];
                struct light_stream_mqueue *queue = light_stream_get_queue(stream);
                if(!light_stream_mqueue_is_empty(queue)) {
                        struct light_message *message;
                        if(message = light_stream_mqueue_try_get(queue)) {
                                light_mutex_do_lock(&stream->lock);
                                if(message->flags & LIGHT_MSG_FASTER) {
                                        stream->handler_va(stream, message->text, message->args);
                                } else {
                                        stream->handler(stream, message->text);
                                }
                                light_mutex_do_unlock(&stream->lock);
                                light_free(message);
                        }
                }
        }
}
void light_stream_init(struct light_stream *stream)
{
        if(streams_defined_count >= LIGHT_STREAM_MAX_STREAMS) {
                light_error("failed to define new output stream: max streams exceeded (%d)", streams_defined_count);
                return;
        }
        streams_defined[streams_defined_count++] = stream;
        stream->handler_va(stream, "opening message stream '%s'\n", light_stream_get_name(stream));
        light_object_init(&stream->obj_header, &ltype_light_stream);
        light_mutex_init(&stream->lock);
        light_stream_mqueue_init(&stream->queue);
}
void light_stream_lock_output(struct light_stream *stream)
{
        light_mutex_do_lock(&stream->lock);
}
void light_stream_unlock_output(struct light_stream *stream)
{
        light_mutex_do_unlock(&stream->lock);
}
void light_stream_mqueue_init(struct light_stream_mqueue *queue)
{
        light_mutex_init(&queue->lock);
        light_condition_init(&queue->write_ready);
        queue->count = 0;
        queue->head = 0;
}
void light_stream_mqueue_add_fast(struct light_stream_mqueue *queue, const uint8_t *text)
{
        light_mutex_do_lock(&queue->lock);
        while(queue->count >= LIGHT_STREAM_MQUEUE_DEPTH) {
                light_condition_wait(&queue->write_ready, &queue->lock);
        }
        uint8_t index = queue->head;
        queue->count++;
        queue->head = (queue->head + 1) % LIGHT_STREAM_MQUEUE_DEPTH;
        queue->message[index] = (struct light_message) {
                .flags = LIGHT_MSG_FAST,
                .text = text
        };
        light_mutex_do_unlock(&queue->lock);
}
void light_stream_mqueue_add_faster(struct light_stream_mqueue *queue, const uint8_t *text, va_list args)
{
        light_mutex_do_lock(&queue->lock);
        while(queue->count >= LIGHT_STREAM_MQUEUE_DEPTH) {
                light_condition_wait(&queue->write_ready, &queue->lock);
        }
        uint8_t index = queue->head;
        queue->count++;
        queue->head = (queue->head + 1) % LIGHT_STREAM_MQUEUE_DEPTH;
        queue->message[index] = (struct light_message) {
                .flags = LIGHT_MSG_FASTER,
                .text = text,
                .args = *args
        };
        light_mutex_do_unlock(&queue->lock);
}
// caller must hold the lock on queue before calling!
static struct light_message *mqueue_take(struct light_stream_mqueue *queue)
{
        struct light_message *message = light_alloc(sizeof(struct light_message));
        uint8_t message_idx = (LIGHT_STREAM_MQUEUE_DEPTH + (queue->head - queue->count)) % LIGHT_STREAM_MQUEUE_DEPTH;

        memcpy(message, &queue->message[message_idx], sizeof(struct light_message));
        queue->count--;
        return message;
}
struct light_message *light_stream_mqueue_get(struct light_stream_mqueue *queue)
{
        light_mutex_do_lock(&queue->lock);
        struct light_message *out = mqueue_take(queue);
        light_mutex_do_unlock(&queue->lock);
        return out;
}
struct light_message *light_stream_mqueue_try_get(struct light_stream_mqueue *queue)
{
        if(light_stream_mqueue_is_empty(queue)) {
                return NULL;
        }
        light_mutex_do_lock(&queue->lock);
        if(light_stream_mqueue_is_empty(queue)) {
                light_mutex_do_unlock(&queue->lock);
                return NULL;
        }
        struct light_message *out = mqueue_take(queue);
        light_mutex_do_unlock(&queue->lock);
        return out;
}
bool light_stream_mqueue_is_empty(struct light_stream_mqueue *queue)
{
        return (atomic_load(&queue->count) == 0);
}
bool light_stream_mqueue_is_full(struct light_stream_mqueue *queue)
{
        return (atomic_load(&queue->count) >= LIGHT_STREAM_MQUEUE_DEPTH);
}

uint8_t light_stream_get_background_logging_mode(struct light_stream *stream)
{
        //return atomic_load(&stream->mode);
        return stream->mode;
}
void light_stream_set_background_logging_mode(struct light_stream *stream, uint8_t mode)
{
        atomic_store(&stream->mode, mode);
}
void light_stream_message_sync(struct light_stream *stream, const uint8_t *message)
{
        light_stream_message_f_sync(stream, message);
}
void light_stream_message_f_sync(struct light_stream *stream, const uint8_t *format, ...)
{
        va_list args;
        va_start(args, format);
        light_stream_message_vf_sync(stream, format, args);
        va_end(args);
}
void light_stream_message_vf_sync(struct light_stream *stream, const uint8_t *format, va_list args)
{
        light_mutex_do_lock(&stream->lock);
        stream->handler_va(stream, format, args);
        light_mutex_do_unlock(&stream->lock);
}
//   'fast' CLI messages are put into a queue that is processed on the main stack,
// but still perform string formatting synchronously before queueing the message
void light_stream_message_fast(struct light_stream *stream, const uint8_t *message)
{
        light_stream_message_f_fast(stream, message);
}
void light_stream_message_f_fast(struct light_stream *stream, const uint8_t *format, ...)
{
        va_list args;
        va_start(args, format);
        light_stream_message_vf_fast(stream, format, args);
        va_end(args);  
}
void light_stream_message_vf_fast(struct light_stream *stream, const uint8_t *format, va_list args)
{
        char *message;
#ifdef __GNUC__
        vasprintf(&message, format, args);
#else
        uint8_t buffer[LIGHT_STREAM_MAX_MSG_LENGTH];
        vsnprintf(&buffer, LIGHT_STREAM_MAX_MSG_LENGTH, format, args);
        message = light_alloc(strlen(&buffer));
        strcpy(message, &buffer);
#endif
        light_stream_mqueue_add_fast(&stream->queue, message);
}
//   'faster' CLI messages defer all CPU-intensive work for asynchronous processing,
// with the consequence that all referenced objects must remain in scope until
// the kernel worker can perform string formatting
void light_stream_message_f_faster(struct light_stream *stream, const uint8_t *format, ...)
{
        va_list args;
        va_start(args, format);
        light_stream_message_vf_fast(stream, format, args);
        va_end(args);  
}
void light_stream_message_vf_faster(struct light_stream *stream, const uint8_t *format, va_list args)
{
        light_stream_mqueue_add_faster(&stream->queue, format, args);
}
