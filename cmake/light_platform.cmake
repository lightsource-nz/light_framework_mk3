macro(light_platform_on_include)
        light_declare(LIGHT_ARCH_LIST)
        light_declare(LIGHT_CHIP_LIST)
        light_declare(LIGHT_BOARD_LIST)
        light_declare(LIGHT_ADD_HW_MODULE_HOOKS)
        light_declare(LIGHT_ADD_ARCH_HOOKS)
        light_declare(LIGHT_ADD_CHIP_HOOKS)
        light_declare(LIGHT_ADD_BOARD_HOOKS)
        light_declare(LIGHT_SELECT_ARCH_HOOKS)
        light_declare(LIGHT_SELECT_CHIP_HOOKS)
        light_declare(LIGHT_SELECT_BOARD_HOOKS)
        light_declare(LIGHT_ARCH)
        light_declare(LIGHT_ARCH_LIB)
        light_declare(LIGHT_CHIP)
        light_declare(LIGHT_CHIP_LIB)
        light_declare(LIGHT_BOARD)
        light_declare(LIGHT_BOARD_LIB)
        light_declare(LIGHT_TARGET_PROPERTY_HOOKS_GLOBAL)

        light_declare(LIGHT_RUN_MODE)
        # mode defaults to production
        if(NOT DEFINED LIGHT_RUN_MODE)
                light_info("LIGHT_RUN_MODE not set, defaulting to PRODUCTION")
                light_set(LIGHT_RUN_MODE PRODUCTION)
        else()
                light_info("LIGHT_RUN_MODE is set to ${LIGHT_RUN_MODE}")
        endif()

        light_declare(LIGHT_SYSTEM)
        # system defaults to build-host native
        if(NOT DEFINED LIGHT_SYSTEM)
                light_info("LIGHT_SYSTEM not set, defaulting to HOST_OS")
                light_set(LIGHT_SYSTEM HOST_OS)
        else()
                light_info("LIGHT_SYSTEM is set to ${LIGHT_SYSTEM}")
        endif()

        light_declare(LIGHT_PLATFORM)
        if(LIGHT_SYSTEM STREQUAL HOST_OS)
                light_set(LIGHT_PLATFORM HOST)
        endif()

        # platform defaults to on-device
        if(NOT DEFINED LIGHT_PLATFORM)
                light_info("LIGHT_PLATFORM not set, defaulting to TARGET")
                light_set(LIGHT_PLATFORM TARGET)
        else()
                light_info("LIGHT_PLATFORM is set to ${LIGHT_PLATFORM}")
        endif()

        light_declare(LIGHT_BOARD)
        if(LIGHT_PLATFORM STREQUAL "HOST" AND NOT DEFINED LIGHT_BOARD)
                light_set(LIGHT_BOARD host_os)
        endif()
        if(NOT DEFINED LIGHT_BOARD)
                light_error("LIGHT_BOARD is not set")
                message(FATAL_ERROR "LIGHT_BOARD must be set to the name of the "
                "target hardware board for all non-host based build configurations "
                "(i.e. those where LIGHT_PLATFORM is set to TARGET or EMULATOR)")
        else()
                light_info("LIGHT_BOARD is set to ${LIGHT_BOARD}")
        endif()
endmacro()

macro(light_add_hw_module MODULE)
        list(APPEND LIGHT_HW_MODULE_LIST "${MODULE}")
        foreach(HOOK IN LISTS LIGHT_ADD_HW_MODULE_HOOKS)
                cmake_language(CALL ${HOOK} ${MODULE})
        endforeach()
endmacro()

macro(light_add_arch ARCH BITS MMU CALLBACK)
        light_debug("added arch ${ARCH}")
        light_declare(LIGHT_ARCH_BITS__${ARCH})
        light_declare(LIGHT_ARCH_MMU__${ARCH})
        light_declare(LIGHT_ARCH_CALLBACK__${ARCH})
        light_set(LIGHT_ARCH_BITS__${ARCH} ${BITS})
        light_set(LIGHT_ARCH_MMU__${ARCH} ${MMU})
        light_set(LIGHT_ARCH_CALLBACK__${ARCH} ${CALLBACK})
        light_append(LIGHT_ARCH_LIST ${ARCH})

        foreach(HOOK IN LISTS LIGHT_ADD_ARCH_HOOKS)
                cmake_language(CALL ${HOOK} ${ARCH})
        endforeach()
endmacro()
macro(light_add_chip CHIP ARCH CALLBACK)
        light_debug("added chip ${CHIP}")
        light_declare(LIGHT_CHIP_ARCH__${CHIP})
        light_declare(LIGHT_CHIP_CALLBACK__${CHIP})
        light_set(LIGHT_CHIP_ARCH__${CHIP} ${ARCH})
        light_set(LIGHT_CHIP_CALLBACK__${CHIP} ${CALLBACK})
        light_append(LIGHT_CHIP_LIST ${CHIP})

        foreach(HOOK IN LISTS LIGHT_ADD_CHIP_HOOKS)
                cmake_language(CALL ${HOOK} ${CHIP})
        endforeach()
endmacro()
macro(light_add_board BOARD CHIP CALLBACK)
        light_debug("added board ${BOARD}")
        light_declare(LIGHT_BOARD_CHIP__${BOARD})
        light_declare(LIGHT_BOARD_CALLBACK__${BOARD})
        light_set(LIGHT_BOARD_CHIP__${BOARD} ${CHIP})
        light_set(LIGHT_BOARD_CALLBACK__${BOARD} ${CALLBACK})
        light_append(LIGHT_BOARD_LIST ${BOARD})

        foreach(HOOK IN LISTS LIGHT_ADD_BOARD_HOOKS)
                cmake_language(CALL ${HOOK} ${BOARD})
        endforeach()
endmacro()
macro(light_select_arch ARCH)
        light_info("arch set to ${ARCH}")
        light_set(LIGHT_ARCH ${ARCH})
        light_set(LIGHT_ARCH_LIB arch_${ARCH})
        light_add_hw_module(${LIGHT_ARCH_LIB})
        
        foreach(HOOK IN LISTS LIGHT_SELECT_ARCH_HOOKS)
                cmake_language(CALL ${HOOK} ${ARCH})
        endforeach()
        cmake_language(CALL ${LIGHT_ARCH_CALLBACK__${ARCH}})
endmacro()
macro(light_select_chip CHIP)
        light_info("chip set to ${CHIP}")
        light_set(LIGHT_CHIP ${CHIP})
        light_set(LIGHT_CHIP_LIB chip_${CHIP})
        light_add_hw_module(${LIGHT_CHIP_LIB})
        
        foreach(HOOK IN LISTS LIGHT_SELECT_CHIP_HOOKS)
                cmake_language(CALL ${HOOK} ${CHIP})
        endforeach()
        cmake_language(CALL ${LIGHT_CHIP_CALLBACK__${CHIP}})
        light_select_arch(${LIGHT_CHIP_ARCH__${CHIP}})
endmacro()
macro(light_select_board BOARD)
        light_info("board set to ${BOARD}")
        light_set(LIGHT_BOARD ${BOARD})
        light_set(LIGHT_BOARD_LIB board_${BOARD})
        light_add_hw_module(${LIGHT_BOARD_LIB})
        
        foreach(HOOK IN LISTS LIGHT_SELECT_BOARD_HOOKS)
                cmake_language(CALL ${HOOK} ${BOARD})
        endforeach()
        cmake_language(CALL ${LIGHT_BOARD_CALLBACK__${BOARD}})
        light_select_chip(${LIGHT_BOARD_CHIP__${BOARD}})
endmacro()

# all hooked 'add' actions should retrospectively pass historic actions to
#       new hooks as they are registered, so that hook functions can
#       respond to actions which took place before the hook was registered
macro(light_hook_add_hw_module HOOK)
        light_append(LIGHT_ADD_HW_MODULE_HOOKS ${HOOK})
        foreach(MODULE IN LISTS LIGHT_HW_MODULE_LIST)
                cmake_language(CALL ${HOOK} ${MODULE})
        endforeach()
endmacro()
macro(light_hook_add_arch HOOK)
        light_append(LIGHT_ADD_ARCH_HOOKS ${HOOK})
        foreach(ARCH IN LISTS LIGHT_ARCH_LIST)
                cmake_language(CALL ${ARCH} ${MODULE})
        endforeach()
endmacro()
macro(light_hook_add_chip HOOK)
        light_append(LIGHT_ADD_CHIP_HOOKS ${HOOK})
        foreach(CHIP IN LISTS LIGHT_CHIP_LIST)
                cmake_language(CALL ${HOOK} ${CHIP})
        endforeach()
endmacro()
macro(light_hook_add_board HOOK)
        light_append(LIGHT_ADD_BOARD_HOOKS ${HOOK})
        foreach(BOARD IN LISTS LIGHT_BOARD_LIST)
                cmake_language(CALL ${HOOK} ${BOARD})
        endforeach()
endmacro()
macro(light_hook_select_arch HOOK)
        light_append(LIGHT_SELECT_ARCH_HOOKS ${HOOK})
        if(DEFINED LIGHT_ARCH)
                cmake_language(CALL ${HOOK} ${LIGHT_ARCH})
        endif()
endmacro()
macro(light_hook_select_chip HOOK)
        light_append(LIGHT_SELECT_CHIP_HOOKS ${HOOK})
        if(DEFINED LIGHT_CHIP)
                cmake_language(CALL ${HOOK} ${LIGHT_CHIP})
        endif()
endmacro()
macro(light_hook_select_board HOOK)
        light_append(LIGHT_SELECT_BOARD_HOOKS ${HOOK})
        if(DEFINED LIGHT_BOARD)
                cmake_language(CALL ${HOOK} ${LIGHT_BOARD})
        endif()
endmacro()

# property value hooks may be global, bound to all properties on a target,
# or bound to a specific property on a specific target
macro(light_hook_set_target_property_fine TARGET HOOK)
        light_append(LIGHT_TARGET_PROPERTY_HOOKS__${TARGET}__${PROPERTY} ${HOOK})
        light_declare(LIGHT_TARGET_PROPERTY_HOOKS__${TARGET}__${PROPERTY})
endmacro()
macro(light_hook_set_target_property TARGET HOOK)
        light_append(APPEND LIGHT_TARGET_PROPERTY_HOOKS__${TARGET} ${HOOK})
        light_declare(LIGHT_TARGET_PROPERTY_HOOKS__${TARGET})
endmacro()
macro(light_hook_set_target_property_global HOOK)
        light_append(APPEND LIGHT_TARGET_PROPERTY_HOOKS_GLOBAL ${HOOK})
endmacro()

macro(light_target_set_property TARGET PROPERTY VALUE)
        set(LIGHT_TARGET_PROPERTY__${TARGET}__${PROPERTY} ${VALUE})
        light_register_common_scope_var(LIGHT_TARGET_PROPERTY__${TARGET}__${PROPERTY})
        light_promote_common_scope_vars()

        foreach(HOOK IN LISTS LIGHT_TARGET_PROPERTY_HOOKS__GLOBAL)
                cmake_language(CALL ${HOOK} ${TARGET} ${PROPERTY} ${VALUE})
        endforeach()
        foreach(HOOK IN LISTS LIGHT_TARGET_PROPERTY_HOOKS__${TARGET})
                cmake_language(CALL ${HOOK} ${TARGET} ${PROPERTY} ${VALUE})
        endforeach()
        foreach(HOOK IN LISTS LIGHT_TARGET_PROPERTY_HOOKS__${TARGET}__${PROPERTY})
                cmake_language(CALL ${HOOK} ${TARGET} ${PROPERTY} ${VALUE})
        endforeach()
endmacro()

light_platform_on_include()
