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
        //   "%s", message -- NOT printf(message). Passing a message as the format string makes
        // every '%' in it a conversion, so a log line quoting a format or a percentage reads
        // arguments that were never passed. It also truncated long lines on RP2, where this
        // reaches pico-sdk's stdio through a 128-byte working buffer
        printf("%s", message);
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
        // "%s", message -- see msg_stdout() for why this must not pass the message as a format
#if (LIGHT_PLATFORM_HAS_STDERR)
        fprintf(stderr, "%s", message);
#else
        printf("%s", message);
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

//   set once the queues exist and may safely be drained. Read by whatever is doing the
// draining -- on LIGHT_PLATFORM_USB_ON_CORE1 that is the platform's core 1 USB worker, which
// is already running by the time light_stream_setup() is reached
volatile bool light_stream_drain_ready = false;

//   these bypass the logging system deliberately: they report on the machinery that logging
// itself is built from, before any of it exists. That makes them raw stdio writes on core 0 --
// harmless everywhere else, but under LIGHT_PLATFORM_USB_ON_CORE1 core 0 touching stdio is
// exactly the cross-core TinyUSB access the whole arrangement exists to prevent, so they are
// compiled out there rather than left as an intermittent race nobody would connect back here
#if LIGHT_PLATFORM_USB_ON_CORE1
#define _stream_boot_printf(...) ((void)0)
#else
#define _stream_boot_printf(...) printf(__VA_ARGS__)
#endif

static void _find_static_streams()
{
        _stream_boot_printf("&__light_streams_start=0x%x, &__light_streams_end=0x%x, sizeof(void *)=0x%x\n", &__light_streams_start, &__light_streams_end, sizeof(void *));
        _stream_boot_printf("((_start - _end = 0x%x) / 0x%x)=0x%x\n",((uintptr_t)&__light_streams_end) - (uintptr_t)&__light_streams_start, sizeof(void *), (((uintptr_t)&__light_streams_end) - ((uintptr_t)&__light_streams_start)) / sizeof(void *));
        static_streams = (struct light_stream **) &__light_streams_start;
        static_stream_count = (((uintptr_t)&__light_streams_end) - ((uintptr_t)&__light_streams_start)) / sizeof(void *);
        _stream_boot_printf("located %d static output streams\n", static_stream_count);
}
static void _load_static_streams()
{
        for(uint16_t i = 0; i < static_stream_count; i++) {
                if(streams_defined_count >= LIGHT_STREAM_MAX_STREAMS)
                        break;
                light_stream_init(static_streams[i]);
        }
        _stream_boot_printf("loaded %d static output streams\n", static_stream_count);
}
#if LIGHT_PLATFORM_HAS_C11_THREADS
static int worker__handle_background_message_streams(void *arg);
#elif LIGHT_PLATFORM_HAS_MULTICORE_WORKER
static void worker__handle_background_message_streams_core1(void);
#else
static uint8_t worker__service_message_queues_task(struct light_application *app);
#endif
// defined below the workers that mostly use it, but light_stream_shutdown() comes first
static bool _all_stream_queues_empty();
void light_stream_setup()
{
        _find_static_streams();
        streams_defined_count = 0;
        _load_static_streams();

#if LIGHT_PLATFORM_USB_ON_CORE1
        //   nothing to launch and nothing to register: light_platform_init() already started
        // the core 1 worker that owns both TinyUSB and this drain, and did so before
        // stdio_init_all() so that enumeration had something pumping it. Registering a core 0
        // drain task here as well would put stdio writes back on the main loop -- both the
        // stall this arrangement removes and the cross-core TinyUSB access it forbids
        light_stream_drain_ready = true;
        light_debug("message queues drained by the core 1 USB worker");
#elif LIGHT_PLATFORM_HAS_C11_THREADS
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
#else
        // there is no worker to stop here -- the queues are drained by a periodic task, and
        // the task scheduler has already stopped by the time this runs. Without this branch
        // everything queued during shutdown is simply discarded, and since the module-unload
        // logging is emitted immediately before this call, that is precisely the output most
        // worth keeping.
        //
        // service_message_queues() takes at most ONE message per queue per call, so this has
        // to loop. bounded rather than looping on the predicate alone: a handler that somehow
        // queued as it drained would otherwise hang the shutdown, and a hang at exit is a
        // worse failure than a dropped line. the bound is the most the queues can hold
        for(uint32_t i = 0; i < LIGHT_STREAM_MAX_STREAMS * LIGHT_STREAM_MQUEUE_DEPTH; i++) {
                if(_all_stream_queues_empty())
                        break;
                light_stream_service_message_queues();
        }
#endif
}

// unguarded: all three drain strategies need it now. the two worker loops use it to keep
// servicing past their stop signal, and light_stream_shutdown() uses it on the single-threaded
// path where there is no worker to do the draining
static bool _all_stream_queues_empty()
{
        for(uint8_t i = 0; i < streams_defined_count; i++) {
                if(!light_stream_mqueue_is_empty(light_stream_get_queue(streams_defined[i])))
                        return false;
        }
        return true;
}
//   blocks until everything queued has actually been written, or until it is clear that is
// not going to happen. Exists for light_fatal(), whose message was reliably LOST: logging is
// asynchronous, so the line explaining why the system is stopping was still sitting in a queue
// when exit() ran. That is the one message worth waiting for -- it is the only account of why
// the device died, and it was the least likely of any to survive.
//
//   the two halves are not interchangeable. Where something else owns the drain -- a worker
// thread, or core 1 under LIGHT_PLATFORM_USB_ON_CORE1 -- this can only WAIT, and must do so on
// a timeout, since a dead or wedged drainer would otherwise turn a clean exit into a hang.
// Where this core is the drainer, waiting would be a deadlock and it has to do the work itself
void light_stream_flush()
{
#if LIGHT_PLATFORM_USB_ON_CORE1 || LIGHT_PLATFORM_HAS_C11_THREADS || LIGHT_PLATFORM_HAS_MULTICORE_WORKER
        for(uint32_t i = 0; i < LIGHT_STREAM_FLUSH_TIMEOUT_MS; i++) {
                if(_all_stream_queues_empty())
                        return;
                light_platform_sleep_ms(1);
        }
#else
        //   bounded by what the queues can hold rather than looping on the predicate alone:
        // a handler that queued as it drained would otherwise hang here, and hanging while
        // reporting a fatal error is a worse failure than dropping a line of it
        for(uint32_t i = 0; i < LIGHT_STREAM_MAX_STREAMS * LIGHT_STREAM_MQUEUE_DEPTH; i++) {
                if(_all_stream_queues_empty())
                        return;
                light_stream_service_message_queues();
        }
#endif
}
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
                        // processed directly out of the queue slot, under a single lock hold,
                        // rather than copying the (~256-byte) struct light_message out to a local
                        // first -- this runs on platforms where the calling stack is only ~2KB
                        // total (e.g. the RP2040 core1 worker), where a message-sized local on top
                        // of the rest of this call chain (plus whatever an interrupt handler adds
                        // on the same stack) is a real overflow risk
                        light_mutex_do_lock(&stream->lock);
                        if(!light_stream_mqueue_is_empty(queue)) {
                                struct light_message *message = light_stream_mqueue_peek(queue);
                                // both message types are fully formatted by the time they're queued
                                // (see light_stream_mqueue_add_fast()/_add_faster()), so there's no
                                // longer a distinction to make here based on message->flags
                                stream->handler(stream, message->text);
                                light_stream_mqueue_advance(queue);
                        }
                        light_mutex_do_unlock(&stream->lock);
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
        //   a DIRECT write, bypassing the queue that does not exist until three lines below.
        // That makes it a core 0 stdio write, and under LIGHT_PLATFORM_USB_ON_CORE1 core 0 is
        // forbidden from touching stdio at all -- core 1 owns TinyUSB by this point, having
        // been launched back in light_platform_init(). Two lines of boot trace are not worth
        // an occasional cross-core collision inside the USB stack
#if !LIGHT_PLATFORM_USB_ON_CORE1
        stream->handler_va(stream, "opening message stream '%s'\n", light_stream_get_name(stream));
#endif
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
        light_condition_init(&queue->write_ready);
        queue->count = 0;
        queue->head = 0;
}
// blocks until there's room, then claims and returns the index of the next slot to
// write into. caller must already hold 'lock', and must fill in the claimed slot's
// .flags/.text before releasing it
static uint8_t mqueue_claim_slot(light_mutex_t *lock, struct light_stream_mqueue *queue)
{
        while(queue->count >= LIGHT_STREAM_MQUEUE_DEPTH) {
                light_condition_wait(&queue->write_ready, lock);
        }
        uint8_t index = queue->head;
        queue->count++;
        queue->head = (queue->head + 1) % LIGHT_STREAM_MQUEUE_DEPTH;
        return index;
}
// formats 'format'+'args' directly into the next available slot's embedded text
// buffer -- no heap or scratch buffer of any kind is involved. acquires and releases
// 'lock' itself
static void mqueue_put_formatted(light_mutex_t *lock, struct light_stream_mqueue *queue, uint8_t flags, const uint8_t *format, va_list args)
{
        light_mutex_do_lock(lock);
        uint8_t index = mqueue_claim_slot(lock, queue);
        queue->message[index].flags = flags;
        vsnprintf((char *)queue->message[index].text, LIGHT_STREAM_MAX_MSG_LENGTH, (const char *)format, args);
        light_mutex_do_unlock(lock);
}
void light_stream_mqueue_add_fast(light_mutex_t *lock, struct light_stream_mqueue *queue, const uint8_t *text)
{
        light_mutex_do_lock(lock);
        uint8_t index = mqueue_claim_slot(lock, queue);
        queue->message[index].flags = LIGHT_MSG_FAST;
        snprintf((char *)queue->message[index].text, LIGHT_STREAM_MAX_MSG_LENGTH, "%s", text);
        light_mutex_do_unlock(lock);
}
void light_stream_mqueue_add_faster(light_mutex_t *lock, struct light_stream_mqueue *queue, const uint8_t *format, va_list args)
{
        mqueue_put_formatted(lock, queue, LIGHT_MSG_FASTER, format, args);
}
// returns a pointer to the oldest queued message without removing it, so it can be
// used in place (e.g. passed straight to a stream's handler) instead of copied out --
// avoiding a ~256-byte stack local for callers running on a tight stack budget (see
// light_stream_service_message_queues()). caller must hold the queue's lock, and the
// queue must not be empty
struct light_message *light_stream_mqueue_peek(struct light_stream_mqueue *queue)
{
        uint8_t message_idx = (LIGHT_STREAM_MQUEUE_DEPTH + (queue->head - queue->count)) % LIGHT_STREAM_MQUEUE_DEPTH;
        return &queue->message[message_idx];
}
// removes the message most recently returned by light_stream_mqueue_peek() and wakes
// any producer waiting for space. caller must hold the queue's lock
void light_stream_mqueue_advance(struct light_stream_mqueue *queue)
{
        queue->count--;
        light_condition_signal(&queue->write_ready);
}
// caller must hold the lock on queue before calling!
static void mqueue_take(struct light_stream_mqueue *queue, struct light_message *out)
{
        *out = *light_stream_mqueue_peek(queue);
        light_stream_mqueue_advance(queue);
}
bool light_stream_mqueue_get(light_mutex_t *lock, struct light_stream_mqueue *queue, struct light_message *out)
{
        light_mutex_do_lock(lock);
        mqueue_take(queue, out);
        light_mutex_do_unlock(lock);
        return true;
}
bool light_stream_mqueue_try_get(light_mutex_t *lock, struct light_stream_mqueue *queue, struct light_message *out)
{
        if(light_stream_mqueue_is_empty(queue)) {
                return false;
        }
        light_mutex_do_lock(lock);
        if(light_stream_mqueue_is_empty(queue)) {
                light_mutex_do_unlock(lock);
                return false;
        }
        mqueue_take(queue, out);
        light_mutex_do_unlock(lock);
        return true;
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
        // through the shared accessor rather than reading the field directly: the setter just
        // below is atomic where the platform has threads, and this was the third place with
        // its own opinion about how to touch `mode`
        return light_stream_get_mode(stream);
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
        //   formats here and calls handler(), rather than passing `args` to handler_va().
        // handler_va() is VARIADIC (see msg_stdout_v()), so handing it a va_list makes the
        // va_list's own representation the first argument the handler then reads: every
        // conversion in the format came out as garbage, and "value is %d" with 42 printed a
        // fragment of a pointer. A message with no conversions was unaffected, which is the
        // kind of partial breakage that survives a casual look.
        //
        //   the 256-byte local is why this is not simply how the whole file works -- see
        // light_stream_service_message_queues(), which goes out of its way to avoid one on the
        // drain path. This is the synchronous path, taken by callers who have already decided
        // to pay for the write in line, and it needs the buffer to format eagerly at all
        uint8_t text[LIGHT_STREAM_MAX_MSG_LENGTH];
        vsnprintf((char *)text, LIGHT_STREAM_MAX_MSG_LENGTH, (const char *)format, args);

        light_mutex_do_lock(&stream->lock);
        stream->handler(stream, (const char *)text);
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
        mqueue_put_formatted(&stream->lock, &stream->queue, LIGHT_MSG_FAST, format, args);
}
//   'faster' CLI messages are formatted immediately, same as 'fast' -- see the comment
// on light_stream_message_vf_faster()'s declaration in light_stream.h
void light_stream_message_f_faster(struct light_stream *stream, const uint8_t *format, ...)
{
        va_list args;
        va_start(args, format);
        light_stream_message_vf_fast(stream, format, args);
        va_end(args);  
}
void light_stream_message_vf_faster(struct light_stream *stream, const uint8_t *format, va_list args)
{
        light_stream_mqueue_add_faster(&stream->lock, &stream->queue, format, args);
}
