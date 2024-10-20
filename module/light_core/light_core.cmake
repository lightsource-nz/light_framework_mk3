macro(light_core_on_include)
        target_compile_definitions(
                light_core INTERFACE
                RUN_MODE=${LIGHT_RUN_MODE}
                SYSTEM=${LIGHT_SYSTEM}
                PLATFORM=${LIGHT_PLATFORM}
        )
        if(DEFINED COMPILE_LOG_LEVEL)
                message("COMPILE_LOG_LEVEL is set to ${COMPILE_LOG_LEVEL}")
                target_compile_definitions(
                        light_core
                        INTERFACE
                        FILTER_LOG_LEVEL=${COMPILE_LOG_LEVEL}
                )
        endif()
        
        light_declare(LIGHT_CORE_LIB_DIR)
        light_declare(LIGHT_CORE_LINK_DIR)
        light_declare(LIGHT_CORE_BOARDS)
        light_declare(LIGHT_CORE_CHIPS)
        light_declare(LIGHT_CORE_ARCHS)
        light_declare(LIGHT_CORE_IMPL_SET)

        light_set(LIGHT_CORE_IMPL_SET 0)
        light_set(LIGHT_CORE_LINK_DIR ${CMAKE_CURRENT_LIST_DIR}/link)

        light_hook_select_board(light_core_select_board_hook)
        light_hook_select_chip(light_core_select_chip_hook)
        light_hook_select_arch(light_core_select_arch_hook)

endmacro()

# called by modules which define implementation libraries for light_object
macro(light_core_add_board_impl BOARD LIB)
        if(NOT "${BOARD}" IN_LIST LIGHT_CORE_BOARDS)
                light_append(LIGHT_CORE_BOARDS ${BOARD})
                light_declare(LIGHT_CORE_BOARD_IMPL__${BOARD})
                light_set(LIGHT_CORE_BOARD_IMPL__${BOARD} ${LIB})
        endif()
endmacro()
macro(light_core_add_chip_impl CHIP LIB)
        if(NOT "${CHIP}" IN_LIST LIGHT_CORE_CHIPS)
                light_append(LIGHT_CORE_CHIPS ${CHIP})
                light_declare(LIGHT_CORE_CHIP_IMPL__${CHIP})
                light_set(LIGHT_CORE_CHIP_IMPL__${CHIP} ${LIB})
        endif()
endmacro()
macro(light_core_add_arch_impl ARCH LIB)
        if(NOT "${ARCH}" IN_LIST LIGHT_CORE_ARCHS)
                light_append(LIGHT_CORE_ARCHS ${ARCH})
                light_declare(LIGHT_CORE_ARCH_IMPL__${ARCH})
                light_set(LIGHT_CORE_ARCH_IMPL__${ARCH} ${LIB})
        endif()
endmacro()

macro(light_core_select_board_hook BOARD)
        if(NOT "${LIGHT_CORE_IMPL_SET}" AND "${BOARD}" IN_LIST LIGHT_CORE_BOARDS)
                target_link_libraries(
                        light_core
                        INTERFACE
                        ${LIGHT_CORE_BOARD_IMPL__${BOARD}}
                )
                light_set(LIGHT_CORE_IMPL_SET 1)
        endif()
endmacro()
macro(light_core_select_chip_hook CHIP)
        if(NOT "${LIGHT_CORE_IMPL_SET}" AND "${CHIP}" IN_LIST LIGHT_CORE_CHIPS)
                target_link_libraries(
                        light_core
                        INTERFACE
                        ${LIGHT_CORE_CHIP_IMPL__${CHIP}}
                )
                light_set(LIGHT_CORE_IMPL_SET 1)
        endif()
endmacro()
macro(light_core_select_arch_hook ARCH)
        if(NOT "${LIGHT_CORE_IMPL_SET}" AND "${ARCH}" IN_LIST LIGHT_CORE_ARCHS)
                target_link_libraries(
                        light_core
                        INTERFACE
                        ${LIGHT_CORE_ARCH_IMPL__${ARCH}}
                )
                light_set(LIGHT_CORE_IMPL_SET 1)
        endif()
        if(NOT ${LIGHT_CORE_IMPL_SET})
                # if we have reached here, our configured platform has no implementation
                message(FATAL_ERROR "no light_core implementation found for board ${LIGHT_BOARD}")
        endif()
endmacro()

function(light_add_hw_module MODULE)
target_link_libraries(
        light_core
        INTERFACE
        ${MODULE}
)
endfunction() 

light_core_on_include()