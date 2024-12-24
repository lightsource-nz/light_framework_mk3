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
        
        #   TODO there are now a range of possible values for PICO_PLATFORM, and this
        # variable must be set early in the load before LIGHT_CHIP is selected
        if(LIGHT_PLATFORM STREQUAL TARGET)
                light_set(PICO_PLATFORM rp2040)
        elseif(LIGHT_PLATFORM STREQUAL HOST)
                light_set(PICO_PLATFORM host)
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
endfunction()
