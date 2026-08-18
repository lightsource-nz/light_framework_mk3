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
        add_subdirectory(module/light_core_chip_stm32f103)
        add_subdirectory(module/light_core_chip_stm32f446)
endif()
add_subdirectory(module/light_core_board_pico_hostmode)
#   built for every platform. This was HOST-only, on the grounds that it "targets terminal-based
# programs (argv parsing, POSIX-flavored I/O)" -- which was not true of the code: there is no
# getopt, no printf/FILE*, no isatty/termios/unistd anywhere in it. Diagnostics go through
# light_core's stream layer, the parser is hand-rolled, and the one POSIX dependency it did have
# (basename) was already replaced by a portable _cli_basename() written for bare-metal newlib.
#
#   as an INTERFACE library this costs a target build nothing unless something links it, and the
# argv entry point every platform already has (light_framework_run) is satisfied on a target by
# a command baked in at build time -- see LIGHT_BOOT_COMMAND
add_subdirectory(module/light_cli)

if(${LIGHT_SYSTEM} STREQUAL PICO_SDK)
        light_load_pico_sdk_support()
elseif(${LIGHT_SYSTEM} STREQUAL CMSIS)
        light_load_cmsis_support()
endif()

#   the transport abstraction over I2C and SPI, with a port per platform. It arrived here from
# its own repository, where it sat beside the display modules and was assumed to belong to them
# -- it linked light_display, light_display linked it back, and the two were a cycle that CMake
# quietly tolerated. Nothing in it ever referenced a light_display type; light_touch and
# light_imu sit on it just as directly. It is a peer of light_core, not of the display layer.
#
#   MUST come after the support block above, not up with the other modules: on a PICO_SDK
# target build its CMakeLists calls pico_generate_pio_header() for the PIO-driven SPI port, and
# that command does not exist until light_load_pico_sdk_support() has initialised the SDK. Added
# any earlier the configure dies with "Unknown CMake command", which names the symptom and not
# the ordering
add_subdirectory(module/light_ioport)

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
if(${LIGHT_SYSTEM} STREQUAL CMSIS AND LIGHT_BOARD STREQUAL bluepill_plus)
        add_subdirectory(test/demo_bluepill_plus)
endif()
if(${LIGHT_SYSTEM} STREQUAL CMSIS AND LIGHT_BOARD STREQUAL weact_stm32f446)
        add_subdirectory(test/demo_weact_stm32f446)
endif()

#   tests. include(CTest) rather than a bare enable_testing(): it defines BUILD_TESTING (ON by
# default), so a consuming project can switch the tests off without editing this, and it is
# what screen-test already uses.
#
#   HOST only, and the guard is here rather than in each test's CMakeLists so those files are
# never even reached on a target build. A cross-compiled test binary could not be run to say
# anything anyway, so `ctest` on a TARGET tree simply finds no tests registered
if(LIGHT_PLATFORM STREQUAL HOST)
        include(CTest)
        if(BUILD_TESTING)
                add_subdirectory(test/demo_cli)
                add_subdirectory(test/test_light_cli_line)
                add_subdirectory(test/test_light_object)
                add_subdirectory(test/test_light_object_concurrent)
                add_subdirectory(test/test_light_log)
                add_subdirectory(test/test_light_list)
                add_subdirectory(test/test_light_stream)
                add_subdirectory(test/test_light_stream_message)
        endif()
endif()

light_select_board(${LIGHT_BOARD})
