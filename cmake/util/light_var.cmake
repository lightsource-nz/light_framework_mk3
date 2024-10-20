if (NOT TARGET _light_var_init_marker)
        add_library(_light_var_init_marker INTERFACE)
macro(light_var_on_include)
        get_directory_property(_NOT_ROOT PARENT_DIRECTORY)
endmacro()

macro(light_declare NAME)
        get_directory_property(_NOT_ROOT PARENT_DIRECTORY)
        if (NOT ${NAME} IN_LIST LIGHT_SHARED_VARS)
                light_append(LIGHT_SHARED_VARS ${NAME})
        else()
                light_warn("multiple declaration of shared variable '${NAME}'")
        endif()
endmacro()

macro(light_set NAME VALUE)
        get_directory_property(_NOT_ROOT PARENT_DIRECTORY)
        if ("${NAME}" IN_LIST LIGHT_SHARED_VARS)
                set(${NAME} ${VALUE})
                if(_NOT_ROOT)
                        set(${NAME} ${VALUE} PARENT_SCOPE)
                endif()
                if(DEFINED LIGHT_VAR_HOOK_LIST__${NAME})
                        foreach(HOOK IN LISTS LIGHT_VAR_HOOK_LIST__${NAME})
                                cmake_language(CALL ${HOOK} ${VALUE})
                        endforeach()
                endif()
        else()
                light_warn("attempted to write to undeclared shared variable '${NAME}'")
        endif()
endmacro()

macro(light_append NAME VALUE)
        get_directory_property(_NOT_ROOT PARENT_DIRECTORY)
        if ("${NAME}" IN_LIST LIGHT_SHARED_VARS)
                list(APPEND ${NAME} ${VALUE})
                if(_NOT_ROOT)
                        set(${NAME} ${${NAME}} PARENT_SCOPE)
                endif()
                if(DEFINED LIGHT_VAR_HOOK_LIST__${NAME})
                        foreach(HOOK IN LISTS LIGHT_VAR_HOOK_LIST__${NAME})
                                cmake_language(CALL ${HOOK} ${${NAME}})
                        endforeach()
                endif()
        else()
                light_warn("attempted to write to undeclared shared variable '${NAME}'")
        endif()
endmacro()


macro(light_promote_shared_vars)
        get_directory_property(_NOT_ROOT PARENT_DIRECTORY)
        if(_NOT_ROOT)
                set(LIGHT_SHARED_VARS ${LIGHT_SHARED_VARS} PARENT_SCOPE)
                foreach(VAR IN LISTS LIGHT_SHARED_VARS)
                        SET(${VAR} ${${VAR}} PARENT_SCOPE)
                endforeach()
        endif()
endmacro()

macro(light_list_shared_vars)
        message("LIGHT_SHARED_VARS = ${LIGHT_SHARED_VARS}")
endmacro()

macro(light_dump_shared_vars)
        foreach(VAR IN LISTS LIGHT_SHARED_VARS)
                message(${VAR} = ${${VAR}})
        endforeach()
endmacro()

# this macro adds a hook which is called when a new variable is declared
macro(light_hook_declare_shared_variable HOOK)
        if (NOT ${NAME} IN_LIST LIGHT_HOOK_DECLARE_LIST)
                light_append(LIGHT_HOOK_DECLARE_LIST ${HOOK})
        endif()
endmacro()

# this macro adds a hook which is called when the value of ${VAR} changes
macro(light_var_hook_value_changed VAR HOOK)
        if (NOT ${VAR} IN_LIST LIGHT_HOOKED_VAR_LIST)
                light_append(LIGHT_HOOKED_VAR_LIST ${VAR})
                light_declare(LIGHT_VAR_HOOK_LIST__${VAR})
        endif()
        light_append(LIGHT_VAR_HOOK_LIST__${VAR} ${HOOK})
endmacro()

set(LIGHT_SHARED_VARS
        LIGHT_SHARED_VARS
        LIGHT_PATH
        LIGHT_RUN_MODE
        LIGHT_SYSTEM
        LIGHT_PLATFORM
        LIGHT_HOOKED_VAR_LIST
        LIGHT_HOOK_ADD_VAR_LIST
)

light_var_on_include()
endif()
