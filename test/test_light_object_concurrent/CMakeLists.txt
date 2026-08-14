#   HOST ONLY, for the same reason as test_light_object -- the binary has to run on the machine
# doing the building -- plus one of its own: these tests need C11 <threads.h>, which is what the
# host port already builds against (LIGHT_PLATFORM_HAS_C11_THREADS). The guard lives in the
# parent CMakeLists so this file is never reached on a target build.
add_executable(test_light_object_concurrent
        ${CMAKE_CURRENT_LIST_DIR}/test_light_object_concurrent.c)

target_link_libraries(test_light_object_concurrent PRIVATE light_core)

#   one CTest case per test function, as in test_light_object.
#
#   KEEP IN SYNC with test_cases[] in test_light_object_concurrent.c. An unknown name here
# exits non-zero rather than passing quietly, so a mismatch surfaces as a failing test rather
# than as silent under-testing; `test_light_object_concurrent --list` prints the real names.
set(LIGHT_OBJECT_CONCURRENT_TEST_CASES
        control_independent_objects
        get_is_atomic
        put_is_atomic
        get_put_balanced
        put_does_not_wrap
        get_rejects_dead_object
        tree_no_double_release
)
foreach(_case ${LIGHT_OBJECT_CONCURRENT_TEST_CASES})
        add_test(NAME light_object_concurrent.${_case}
                COMMAND test_light_object_concurrent ${_case})

        #   TIMEOUT: these run many thousands of contended operations across several rounds, so
        # they are slower than the sequential suite and slower again on a loaded machine. Stated
        # explicitly rather than left to the default, so an overrun reads as "took too long"
        # rather than being mistaken for a concurrency failure.
        #
        #   RUN_SERIAL: each case saturates the machine with THREAD_COUNT contended threads, and
        # `ctest -j` would otherwise run several of them against each other. That would not make
        # them wrong, but it changes the interleaving they exist to control, and a suite whose
        # result depends on how it was invoked is not worth much
        set_tests_properties(light_object_concurrent.${_case}
                PROPERTIES TIMEOUT 300 RUN_SERIAL TRUE)
endforeach()
