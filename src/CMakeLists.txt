# src/CMakeLists.txt

# Copyright (c) 2025 Omar Berrow

set(SERVER_SOURCES "server_main.c" "con.c")

add_executable(obos-aud ${SERVER_SOURCES})

target_link_libraries(obos-aud PRIVATE autrans_so m)

target_compile_definitions(obos-aud PRIVATE BUILDING_OBOS_AUD_SERVER=1)