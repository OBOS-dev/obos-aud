# src/backends/file/CMakeLists.txt

# Copyright (c) 2025 Omar Berrow

set(FILE_BACKEND_SOURCES "impl.c")

add_library(backend_obj OBJECT ${FILE_BACKEND_SOURCES})

target_compile_definitions(backend_obj PRIVATE BUILDING_OBOS_AUD_SERVER=1)