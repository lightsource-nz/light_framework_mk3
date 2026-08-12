#ifndef _LIGHT_PLATFORM_PORT_H
#define _LIGHT_PLATFORM_PORT_H

#define LIGHT_PLATFORM_HAS_STDERR 1
#define LIGHT_PLATFORM_HAS_C11_THREADS 1
// no PWM on a host build. light_platform_pwm_open() returns NULL, and the device libraries
// that use it then create no device -- the same path a board with no buzzer already takes, so
// nothing downstream needs a special case for it
#define LIGHT_PLATFORM_HAS_PWM 0
#define LIGHT_PLATFORM_HAS_PWM_STREAM 0

#endif