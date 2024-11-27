cmake_minimum_required(VERSION 3.18)

include(${CMAKE_CURRENT_LIST_DIR}/light_init.cmake)
if(${LIGHT_SYSTEM} STREQUAL PICO_SDK)
        include(pico_sdk)
endif()
include(light_platform)

project(light_framework_mk3 C CXX ASM)


# modules
add_subdirectory(module/light_core)
add_subdirectory(module/light_core_arch_arm32_m)
add_subdirectory(module/light_core_arch_host_system)
add_subdirectory(module/light_core_chip_rp2040)
add_subdirectory(module/light_cli)

light_info("loading platform support database...")
add_subdirectory(platform/arch)
add_subdirectory(platform/chip)
add_subdirectory(platform/board)

if(${LIGHT_SYSTEM} STREQUAL PICO_SDK)
        light_load_pico_sdk_support()
endif()

# tests
add_subdirectory(test/framework_demo)

light_select_board(${LIGHT_BOARD})

if(NOT_ROOT)
        light_promote_shared_vars()
endif()
