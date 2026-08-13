cmake_minimum_required(VERSION 3.18)

include(${CMAKE_CURRENT_LIST_DIR}/light_init.cmake)
# must run before the LIGHT_SYSTEM check below: this is where LIGHT_SYSTEM (and LIGHT_PLATFORM,
# LIGHT_BOARD) get their defaults established, so checking them any earlier sees an empty value
include(light_platform)
if(${LIGHT_SYSTEM} STREQUAL PICO_SDK)
        include(pico_sdk)
elseif(${LIGHT_SYSTEM} STREQUAL CMSIS)
        # must be before project(): light_cmsis_select_toolchain() sets CMAKE_TOOLCHAIN_FILE,
        # and CMake probes the compiler during project(). Setting it later is silently ignored
        # and the build uses the host compiler
        include(cmsis)
        light_cmsis_select_toolchain()
endif()

project(light_framework_mk3 C CXX ASM)

find_package(Sanitizers)

# modules
add_subdirectory(module/light_core)
add_subdirectory(module/light_core_arch_host_os)
# before the two chip ports that link it. as an INTERFACE library its own dependencies are
# only resolved through a consumer, so the Pico SDK targets it names cost a HOST build
# nothing -- nothing links the chip ports there
add_subdirectory(module/light_core_chip_rp2_common)
add_subdirectory(module/light_core_chip_rp2040)
add_subdirectory(module/light_core_chip_rp2350)
# only for a CMSIS build: it names the vendored CMSIS sources, and its CMakeLists errors out if
# those submodules are absent. Guarding it means a HOST or PICO_SDK build of this project needs
# no CMSIS checkout at all, which is the same courtesy the rp2 ports get from being INTERFACE
# libraries nothing links on a host build
if(${LIGHT_SYSTEM} STREQUAL CMSIS)
        # before the chip ports that link it, as with light_core_chip_rp2_common above
        add_subdirectory(module/light_core_chip_stm32_common)
        add_subdirectory(module/light_core_chip_stm32f411)
        add_subdirectory(module/light_core_chip_stm32h743)
endif()
add_subdirectory(module/light_core_board_pico_hostmode)
# light_cli currently targets terminal-based programs (argv parsing, POSIX-flavored I/O); only
# built for HOST for now
if(LIGHT_PLATFORM STREQUAL HOST)
        add_subdirectory(module/light_cli)
endif()

if(${LIGHT_SYSTEM} STREQUAL PICO_SDK)
        light_load_pico_sdk_support()
elseif(${LIGHT_SYSTEM} STREQUAL CMSIS)
        light_load_cmsis_support()
endif()

light_info("loading platform support database...")
add_subdirectory(platform/arch)
add_subdirectory(platform/chip)
add_subdirectory(platform/board)

# the one executable a bare-CMSIS build produces, and so the only thing that compiles the
# STM32F411 port at all -- every module here is an INTERFACE library. added after the platform
# database, since it links whatever board was selected
if(${LIGHT_SYSTEM} STREQUAL CMSIS AND LIGHT_BOARD STREQUAL blackpill)
        add_subdirectory(test/demo_blackpill)
endif()
if(${LIGHT_SYSTEM} STREQUAL CMSIS AND LIGHT_BOARD STREQUAL mini_stm32h7)
        add_subdirectory(test/demo_mini_stm32h7)
endif()

# tests
if(LIGHT_PLATFORM STREQUAL HOST)
        enable_testing()
        add_subdirectory(test/demo_cli)
endif()

light_select_board(${LIGHT_BOARD})
