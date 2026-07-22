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

#if LIGHT_PLATFORM_HAS_C11_THREADS
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
#if LIGHT_PLATFORM_HAS_C11_THREADS
static thrd_t worker_thread;
static atomic_bool flag_worker_online;
static atomic_bool worker_should_stop;
#endif

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
#if LIGHT_PLATFORM_HAS_C11_THREADS
static int worker__handle_background_message_streams(void *arg);
#elif LIGHT_PLATFORM_HAS_MULTICORE_WORKER
static void worker__handle_background_message_streams_core1(void);
#else
static uint8_t worker__service_message_queues_task(struct light_application *app);
#endif
void light_stream_setup()
{
        _find_static_streams();
        streams_defined_count = 0;
        _load_static_streams();

#if LIGHT_PLATFORM_HAS_C11_THREADS
        atomic_store(&flag_worker_online, false);
        if(0 != thrd_create(&worker_thread, worker__handle_background_message_streams, NULL)) {
                light_fatal("failed to launch background messaging worker thread");
        }
        // wait for the worker to signal it's up. this is a one-time, sub-millisecond handshake,
        // so a plain atomic poll is simpler than a mutex+condvar rendezvous here -- and since
        // there's no signal to miss, it sidesteps that whole class of bug entirely
        // (thrd_yield() isn't implemented by this platform's C11 threads shim, hence thrd_sleep())
        struct timespec poll_interval = { .tv_sec = 0, .tv_nsec = 100000 };
        while(!atomic_load(&flag_worker_online)) {
                thrd_sleep(&poll_interval, NULL);
        }
        light_debug("background messaging worker launched");
#elif LIGHT_PLATFORM_HAS_MULTICORE_WORKER
        // no OS threads on bare-metal RP2040, but there is a second physical CPU core --
        // run the worker there instead
        light_core_port_worker_launch(worker__handle_background_message_streams_core1);
        light_debug("background messaging worker launched on core 1");
#else
        // single-core bare metal with no worker of any kind: drain the queues from the main
        // task loop instead (see framework.c's periodic task scheduler)
        light_module_register_periodic_task(&light_core, "stream_service", worker__service_message_queues_task);
#endif
}
void light_stream_shutdown()
{
#if LIGHT_PLATFORM_HAS_C11_THREADS
        atomic_store(&worker_should_stop, true);
        thrd_join(worker_thread, NULL);
#elif LIGHT_PLATFORM_HAS_MULTICORE_WORKER
        light_core_port_worker_signal_stop();
        light_core_port_worker_join();
#endif
}

#if LIGHT_PLATFORM_HAS_C11_THREADS || LIGHT_PLATFORM_HAS_MULTICORE_WORKER
static bool _all_stream_queues_empty()
{
        for(uint8_t i = 0; i < streams_defined_count; i++) {
                if(!light_stream_mqueue_is_empty(light_stream_get_queue(streams_defined[i])))
                        return false;
        }
        return true;
}
#endif
#if LIGHT_PLATFORM_HAS_C11_THREADS
static int worker__handle_background_message_streams(void *arg)
{
        atomic_store(&flag_worker_online, true);
        // keep servicing queues past the stop signal until they're fully drained, otherwise
        // messages queued just before shutdown (e.g. module unload logging) get lost
        while(!atomic_load(&worker_should_stop) || !_all_stream_queues_empty()) {
                light_stream_service_message_queues();
        }
        return 0;
}
#elif LIGHT_PLATFORM_HAS_MULTICORE_WORKER
static void worker__handle_background_message_streams_core1(void)
{
        // keep servicing queues past the stop signal until they're fully drained, otherwise
        // messages queued just before shutdown (e.g. module unload logging) get lost
        while(!light_core_port_worker_stop_requested() || !_all_stream_queues_empty()) {
                light_stream_service_message_queues();
        }
        light_core_port_worker_signal_finished();
}
#else
static uint8_t worker__service_message_queues_task(struct light_application *app)
{
        light_stream_service_message_queues();
        return LF_STATUS_RUN;
}
#endif
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
        queue->message[index].flags = LIGHT_MSG_FASTER;
        queue->message[index].text = text;
        va_copy(queue->message[index].args, args);
        light_mutex_do_unlock(&queue->lock);
}
// caller must hold the lock on queue before calling!
static struct light_message *mqueue_take(struct light_stream_mqueue *queue)
{
        // FIXME I'm certain this heap allocation is totally redundant, but removing
        // it entails changing the API, so I'm just leaving a note for now
        struct light_message *message = light_alloc(sizeof(struct light_message));
        uint8_t message_idx = (LIGHT_STREAM_MQUEUE_DEPTH + (queue->head - queue->count)) % LIGHT_STREAM_MQUEUE_DEPTH;

        memcpy(message, &queue->message[message_idx], sizeof(struct light_message));
        queue->count--;
        light_condition_signal(&queue->write_ready);
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
#if LIGHT_PLATFORM_HAS_C11_THREADS
        return (atomic_load(&queue->count) == 0);
#else
        return (queue->count == 0);
#endif
}
bool light_stream_mqueue_is_full(struct light_stream_mqueue *queue)
{
#if LIGHT_PLATFORM_HAS_C11_THREADS
        return (atomic_load(&queue->count) >= LIGHT_STREAM_MQUEUE_DEPTH);
#else
        return (queue->count >= LIGHT_STREAM_MQUEUE_DEPTH);
#endif
}

uint8_t light_stream_get_background_logging_mode(struct light_stream *stream)
{
        return stream->mode;
}
void light_stream_set_background_logging_mode(struct light_stream *stream, uint8_t mode)
{
#if LIGHT_PLATFORM_HAS_C11_THREADS
        atomic_store(&stream->mode, mode);
#else
        stream->mode = mode;
#endif
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
        message = light_alloc(strlen(&buffer) + 1);
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
