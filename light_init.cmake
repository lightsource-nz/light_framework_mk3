if (NOT TARGET _light_init_marker)
        add_library(_light_init_marker INTERFACE)
        get_filename_component(LIGHT_PATH "${LIGHT_PATH}" REALPATH BASE_DIR "${CMAKE_SOURCE_DIR}")

        list(APPEND CMAKE_MODULE_PATH ${LIGHT_PATH}/cmake)
        set(CMAKE_MODULE_PATH "${LIGHT_PATH}/cmake" ${CMAKE_MODULE_PATH})
        set(CMAKE_MODULE_PATH "${LIGHT_PATH}/cmake/sanitizers/cmake" ${CMAKE_MODULE_PATH})
        include(util/light_var)
        include(util/light_log)
        # included here rather than from either of its two call sites, because those sites
        # (external/light_preinit.cmake and cmake/pico_sdk.cmake) both already include this
        # file and run at points where the other has not necessarily been loaded
        include(util/light_pico_platform)
endif()
