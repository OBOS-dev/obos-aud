# CMakeLists.txt

# Copyright (c) 2025 Omar Berrow

cmake_minimum_required(VERSION 3.10)

option(ENABLE_SHARED "Whether to build shared libraries" ON)
option(ENABLE_STATIC "Whether to build static libraries" OFF)
option(ENABLE_ASAN "Whether to build with ASAN" OFF)

set (BACKEND obos-hda CACHE STRING "The backend to use for the server")

if (NOT ${BACKEND} STREQUAL "obos-hda" AND NOT ${BACKEND} STREQUAL "file")
    message(FATAL_ERROR "Invalid backend! BACKEND can be 'file' or 'obos-hda'")
endif()

if (ENABLE_ASAN)
    add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
    add_link_options(-fsanitize=address)
endif()

project(obos-aud C)

include_directories(${CMAKE_SOURCE_DIR}/include)

add_subdirectory(libautrans)
add_subdirectory(src)
add_subdirectory(test)