# board_host_system: board support for host system board model
add_library(board_host_system INTERFACE)

function(board_host_system_select)

endfunction()

light_add_board(host_system host_system board_host_system_select)

target_include_directories(
    board_host_system
    INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/include
)
 
target_sources(
    board_host_system
    INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/src/board.c
)
