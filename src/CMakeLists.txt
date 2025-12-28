# src/CMakeLists.txt

# Copyright (c) 2025 Omar Berrow

add_subdirectory(backends/${BACKEND})

set(SERVER_SOURCES "server_main.c" "con.c" "mixer.c" "stream.c")

add_executable(obos-aud ${SERVER_SOURCES} $<TARGET_OBJECTS:backend_obj>)

target_link_libraries(obos-aud PRIVATE autrans_so m)

target_compile_definitions(obos-aud PRIVATE BUILDING_OBOS_AUD_SERVER=1)

install(TARGETS obos-aud)