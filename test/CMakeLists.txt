# test/CMakeLists.txt

# Copyright (c) 2025 Omar Berrow

set(TEST_SOURCES "client_main.c")

add_executable(test ${TEST_SOURCES})

target_link_libraries(test PRIVATE autrans_so)
