# cmake/pico_sdk.cmake
# defines light framework integration with the Raspberry Pi Pico SDK
macro(light_pico_sdk_target_property_hook TARGET PROPERTY VALUE)
        if(${PROPERTY} STREQUAL "application_target" AND ${VALUE} EQUAL 1)
                pico_add_extra_outputs(${TARGET})
        endif()
endmacro()

function(light_load_pico_sdk_support)
        message("Loading pico-sdk support")
        light_declare(PICO_PLATFORM)
        light_declare(PICO_BOARD)
        light_declare(PICO_SDK_PATH)
        
        set(_light_pico_host_mingw_workaround OFF)
        #   TODO there are now a range of possible values for PICO_PLATFORM, and this
        # variable must be set early in the load before LIGHT_CHIP is selected
        if(LIGHT_PLATFORM STREQUAL TARGET)
                # this duplicates external/light_preinit.cmake's own board->platform
                # mapping (a second, separate place PICO_PLATFORM gets set -- this one
                # runs later, during add_subdirectory(light_framework), overwriting
                # whatever light_preinit.cmake already resolved right before the
                # pico_sdk_init() call that actually matters, so it has to agree). same
                # reasoning applies: pico-sdk doesn't derive PICO_PLATFORM from PICO_BOARD
                # on its own, and rp2350-arm-s (not generic "rp2350") is the correct value
                # for Pico 2
                if(LIGHT_BOARD STREQUAL pico2 OR LIGHT_BOARD STREQUAL pico2_w
                                OR LIGHT_BOARD STREQUAL waveshare_rp2350_touch_lcd_1.69)
                        light_set(PICO_PLATFORM rp2350-arm-s)
                else()
                        light_set(PICO_PLATFORM rp2040)
                endif()
        elseif(LIGHT_PLATFORM STREQUAL HOST)
                light_set(PICO_PLATFORM host)
                if(MINGW)
                        set(_light_pico_host_mingw_workaround ON)
                endif()
        endif()

        if(DEFINED ENV{PICO_SDK_PATH} AND (NOT PICO_SDK_PATH))
                light_set(PICO_SDK_PATH $ENV{PICO_SDK_PATH})
        endif()

        cmake_path(IS_RELATIVE PICO_SDK_PATH _SDK_PATH_RELATIVE)
        if(${_SDK_PATH_RELATIVE})
                light_expand_relative_path(PICO_SDK_PATH "${PICO_SDK_PATH}" ${CMAKE_BINARY_DIR})
        endif()
        
        light_set(PICO_SDK_PATH "${PICO_SDK_PATH}")
        set(PICO_SDK_PATH "${PICO_SDK_PATH}" CACHE PATH "Path to the Raspberry Pi Pico SDK" FORCE)
        
        light_hook_set_target_property_global(light_pico_sdk_target_property_hook)

        include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)
        pico_sdk_init()

        if(_light_pico_host_mingw_workaround)
                # MinGW's <time.h> only exposes localtime_r() (used by pico-sdk's
                # src/common/pico_util/datetime.c) when _POSIX_C_SOURCE is defined. pico_util
                # is an INTERFACE library whose sources compile fresh into whichever target
                # actually links it, following the dependency graph rather than the directory
                # tree, so this must be a target_compile_definitions() on pico_util itself to
                # reach every consumer -- a directory-scoped add_compile_definitions() here
                # would only reach targets defined under light_framework's own subtree, missing
                # sibling subtrees like a consuming project's own demo/test executables
                target_compile_definitions(pico_util INTERFACE _POSIX_C_SOURCE=200809L)
        endif()
endfunction()
