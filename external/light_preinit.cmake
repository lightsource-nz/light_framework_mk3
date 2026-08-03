if (NOT TARGET _light_preinit_marker)
        add_library(_light_preinit_marker INTERFACE)
        get_filename_component(LIGHT_PATH "${LIGHT_PATH}" REALPATH BASE_DIR "${CMAKE_SOURCE_DIR}")

        set(LIGHT_INIT_CMAKE_FILE ${LIGHT_PATH}/light_init.cmake)
        if(NOT EXISTS ${LIGHT_INIT_CMAKE_FILE})
            message(FATAL_ERROR "Directory '${LIGHT_PATH}' does not appear to contain the Light Framework SDK")
        endif()

        include(${LIGHT_INIT_CMAKE_FILE})

        if(LIGHT_SYSTEM STREQUAL PICO_SDK)
                light_declare(PICO_SDK_PATH)
                if(LIGHT_PLATFORM STREQUAL TARGET)
                        # pico-sdk doesn't derive PICO_PLATFORM from PICO_BOARD itself (its
                        # own default is unconditionally rp2040 too -- see
                        # pico-sdk/cmake/pico_pre_load_platform.cmake), so this has to be
                        # set explicitly per board. rp2350-arm-s (not a generic "rp2350") is
                        # the correct value for Pico 2 -- matches what tinyusb's own
                        # hw/bsp/rp2040/boards/raspberry_pi_pico2/board.cmake sets
                        if(LIGHT_BOARD STREQUAL pico2 OR LIGHT_BOARD STREQUAL pico2_w)
                                set(PICO_PLATFORM rp2350-arm-s)
                        else()
                                set(PICO_PLATFORM rp2040)
                        endif()
                        set(PICO_BOARD "${LIGHT_BOARD}")
                elseif(LIGHT_PLATFORM STREQUAL HOST)
                        set(PICO_PLATFORM host)
                endif()

                if(DEFINED ENV{PICO_SDK_PATH} AND (NOT PICO_SDK_PATH))
                        set(PICO_SDK_PATH $ENV{PICO_SDK_PATH})
                endif()

                cmake_path(IS_RELATIVE PICO_SDK_PATH _SDK_PATH_RELATIVE)
                if(${_SDK_PATH_RELATIVE})
                        light_expand_relative_path(PICO_SDK_PATH "${PICO_SDK_PATH}" ${CMAKE_BINARY_DIR})
                endif()

                light_set(PICO_SDK_PATH "${PICO_SDK_PATH}")
                set(PICO_SDK_PATH "${PICO_SDK_PATH}" CACHE PATH "Path to the Raspberry Pi Pico SDK" FORCE)

                include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)
        endif()
endif()