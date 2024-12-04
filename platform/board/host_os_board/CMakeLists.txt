# board_host_os: board support for host system board model
add_library(board_host_os INTERFACE)

function(board_host_os_select)

endfunction()

light_add_board(host_os host_os board_host_os_select)

target_include_directories(
    board_host_os
    INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/include
)
 
target_sources(
    board_host_os
    INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/src/board.c
)
