# 'light' software application framework
the goal of the light framework is to create a C Language-based application framework and build system, which understands the concept of software that runs on the local development machine, as well as software that is targeted to run on some external embedded system, and facilitates testing of embedded applications on local hardware, as well as other complex building and deployment scenarios. light also aims to facilitate the construction of applications and libraries in a modular fashion, and to generally make it easier for developers to create new software projects, and new modules within existing projects.

## requirements
- light mk3-alpha currently requires CMake version 3.25 or higher as it requires version 6 of the cmake preset JSON schema
- embedded applications and libraries using the Raspberry Pi Pico SDK require Pico SDK version 2.0.0 or higher
- all configurations require a C compiler with support for the "C11" ISO standard. development is done primarily using GCC on Linux for native/host-based applications, and the ARM GNU embedded toolchain on Linux (for cortex-m targets) for embedded applications. however, effort is made to support as wide of a variety of native and cross-compiling toolchains, and development host platforms, as possible
