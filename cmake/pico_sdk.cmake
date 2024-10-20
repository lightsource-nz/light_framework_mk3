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

        if(LIGHT_PLATFORM STREQUAL TARGET)
                set(PICO_PLATFORM rp2040)
        elseif(LIGHT_PLATFORM STREQUAL HOST)
                set(PICO_PLATFORM host)
        endif()

        if(${NOT_ROOT})
                set(PICO_PLATFORM ${PICO_PLATFORM} PARENT_SCOPE)
                set(PICO_BOARD ${PICO_BOARD} PARENT_SCOPE)
        endif()

        get_filename_component(PICO_SDK_PATH "${PICO_SDK_PATH}" REALPATH BASE_DIR "${CMAKE_BINARY_DIR}")
        include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)
        pico_sdk_init()
        
endfunction()
