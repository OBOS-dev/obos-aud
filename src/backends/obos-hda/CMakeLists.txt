# src/backends/obos-hda/CMakeLists.txt

# Copyright (c) 2025 Omar Berrow

if (CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
    set (SYSCALL "x86_64-syscall.S")
else()
    message(FATAL_ERROR "Unknown architecture \"" ${CMAKE_SYSTEM_PROCESSOR} "\"")
endif()

set(FILE_BACKEND_SOURCES "impl.c" ${SYSCALL})

add_library(backend_obj OBJECT ${FILE_BACKEND_SOURCES})

target_compile_definitions(backend_obj PRIVATE BUILDING_OBOS_AUD_SERVER=1)