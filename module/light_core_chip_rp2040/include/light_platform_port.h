#ifndef _LIGHT_PLATFORM_PORT_H
#define _LIGHT_PLATFORM_PORT_H

#define LIGHT_PLATFORM_HAS_STDERR 0
#define LIGHT_PLATFORM_HAS_C11_THREADS 0
// RP2040 has no OS threads, but it does have a second physical CPU core: the background
// message-stream worker runs there instead (see light_core_port_worker_launch() below and its
// use in light_core/src/stream.c)
#define LIGHT_PLATFORM_HAS_MULTICORE_WORKER 1

#endif
