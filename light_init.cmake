if (NOT TARGET _light_init_marker)
        add_library(_light_init_marker INTERFACE)
        get_filename_component(LIGHT_PATH "${LIGHT_PATH}" REALPATH BASE_DIR "${CMAKE_SOURCE_DIR}")

        list(APPEND CMAKE_MODULE_PATH ${LIGHT_PATH}/cmake)
        include(util/light_var)
        include(util/light_log)
endif()
