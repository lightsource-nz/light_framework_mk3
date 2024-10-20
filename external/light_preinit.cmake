get_filename_component(LIGHT_PATH "${LIGHT_PATH}" REALPATH BASE_DIR "${CMAKE_SOURCE_DIR}")

set(LIGHT_INIT_CMAKE_FILE ${LIGHT_PATH}/light_init.cmake)
if(NOT EXISTS ${LIGHT_INIT_CMAKE_FILE})
    message(FATAL_ERROR "Directory '${LIGHT_PATH}' does not appear to contain the Light Framework SDK")
endif()

include(${LIGHT_INIT_CMAKE_FILE})

if(LIGHT_SYSTEM STREQUAL PICO_SDK)
        if(LIGHT_PLATFORM STREQUAL TARGET)
                set(PICO_PLATFORM rp2040)
                set(PICO_BOARD "${LIGHT_BOARD}")
        elseif(LIGHT_PLATFORM STREQUAL HOST)
                set(PICO_PLATFORM host)
        endif()

        #set(PICO_PLATFORM ${PICO_PLATFORM} PARENT_SCOPE)
        get_filename_component(PICO_SDK_PATH "${PICO_SDK_PATH}" REALPATH BASE_DIR "${CMAKE_BINARY_DIR}")

        include(${PICO_SDK_PATH}/external/pico_sdk_import.cmake)
endif()
