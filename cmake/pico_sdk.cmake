function(light_pico_sdk_enable)
        light_hook_set_target_property_global(light_pico_sdk_target_property_hook)
endfunction()

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

        if(LIGHT_PLATFORM STREQUAL TARGET)
                light_set(PICO_PLATFORM rp2040)
        elseif(LIGHT_PLATFORM STREQUAL HOST)
                light_set(PICO_PLATFORM host)
        endif()

        light_expand_relative_path(PICO_SDK_PATH "${PICO_SDK_PATH}" ${CMAKE_BINARY_DIR})
        include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)
        pico_sdk_init()
        
endfunction()
