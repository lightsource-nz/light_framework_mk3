# HOST ONLY. These run the real light_core and inspect what it did, so the binary has to
# execute on the machine doing the building -- a cross-compiled one would need an emulator to
# say anything at all. The guard lives in the parent CMakeLists so this file is never reached
# on a target build.
add_executable(test_light_object ${CMAKE_CURRENT_LIST_DIR}/test_light_object.c)

# light_core is an INTERFACE library carrying its own sources and its port selection, so
# linking it is all the test needs -- the same reason rend's tests link rend rather than
# assembling include paths by hand
target_link_libraries(test_light_object PRIVATE light_core)

#   one CTest case per test function rather than one for the whole binary: a failure then names
# itself in the ctest summary instead of being one line inside a wall of output. The binary
# takes a case name and runs only that one.
#
#   KEEP IN SYNC with test_cases[] in test_light_object.c. `test_light_object --list` prints the
# names the binary actually has, and an unknown name here exits non-zero rather than passing
# quietly, so a mismatch shows up as a failing test rather than as silent under-testing.
set(LIGHT_OBJECT_TEST_CASES
        init_sets_one_reference
        get_increments
        put_decrements
        get_put_balanced
        get_on_dead_object_returns_null
        put_does_not_underflow
        get_null_is_safe
        add_sets_name_and_parent
        add_takes_parent_reference
        add_rejects_empty_name
        add_truncates_long_name
        add_fires_events
        release_fires_at_zero
        release_not_fired_while_referenced
        release_fires_once_only
        release_skipped_for_static_object
        init_clears_static_flag
        del_detaches_from_parent
        del_releases_parent_reference
        del_is_idempotent
        del_fires_child_remove_before_release
)
foreach(_case ${LIGHT_OBJECT_TEST_CASES})
        add_test(NAME light_object.${_case} COMMAND test_light_object ${_case})
endforeach()
