macro(light_log_on_include)
        # define local symbols
        set(LEVEL_TRACE 4)
        set(LEVEL_DEBUG 3)
        set(LEVEL_INFO 2)
        set(LEVEL_WARN 1)
        set(LEVEL_ERROR 0)

        # log level silently defaults to INFO
        if(NOT DEFINED LIGHT_OUTPUT_LEVEL)
                set(LIGHT_OUTPUT_LEVEL INFO)
        endif()

endmacro()
macro(light_log LEVEL MESSAGE)
        # compare numeric values of log levels to determine if this message is logged
        if(${LEVEL_${LEVEL}} LESS_EQUAL ${LEVEL_${LIGHT_OUTPUT_LEVEL}})
                message("${LEVEL}: ${MESSAGE}")
        endif()
endmacro()

macro(light_trace MESSAGE)
        light_log(TRACE "${MESSAGE}")
endmacro()
macro(light_debug MESSAGE)
        light_log(DEBUG "${MESSAGE}")
endmacro()
macro(light_info MESSAGE)
        light_log(INFO "${MESSAGE}")
endmacro()
macro(light_warn MESSAGE)
        light_log(WARN "${MESSAGE}")
endmacro()
macro(light_error MESSAGE)
        light_log(ERROR "${MESSAGE}")
endmacro()

light_log_on_include()
