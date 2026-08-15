#   HOST ONLY, like the other suites. Nothing in list.c is host-specific -- it operates on a
# caller's array and count and calls nothing -- but the binary still has to run on the machine
# doing the building. The guard lives in the parent CMakeLists so this file is never reached on
# a target build.
add_executable(test_light_list ${CMAKE_CURRENT_LIST_DIR}/test_light_list.c)

target_link_libraries(test_light_list PRIVATE light_core)

#   one CTest case per test function, as in the other suites: a failure names itself in the
# ctest summary rather than being one line inside a wall of output.
#
#   KEEP IN SYNC with test_cases[] in test_light_list.c. An unknown name here exits non-zero
# rather than passing quietly, so a mismatch surfaces as a failing test rather than as silent
# under-testing; `test_light_list --list` prints the names the binary actually has.
set(LIGHT_LIST_TEST_CASES
        indexof_finds_each_position
        indexof_reports_absent_as_negative
        indexof_respects_the_count
        append_adds_in_order
        delete_at_index_shortens_the_list
        delete_at_index_first_and_last
        delete_at_index_stays_in_bounds
        delete_to_empty
        delete_at_index_ignores_an_out_of_range_index
        delete_item_removes_only_that_item
        delete_item_ignores_an_absent_item
        insert_in_the_middle_keeps_the_tail
        insert_at_the_front
        insert_at_the_end_appends
        insert_beyond_the_end_clamps
)
foreach(_case ${LIGHT_LIST_TEST_CASES})
        add_test(NAME light_list.${_case} COMMAND test_light_list ${_case})
endforeach()
