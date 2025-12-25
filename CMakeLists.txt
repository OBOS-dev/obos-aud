# CMakeLists.txt

# Copyright (c) 2025 Omar Berrow

cmake_minimum_required(VERSION 3.10)

option(ENABLE_SHARED "Whether to build shared libraries" ON)
option(ENABLE_STATIC "Whether to build static libraries" OFF)

project(obos-aud C)

include_directories(${CMAKE_SOURCE_DIR}/include)

add_subdirectory(libautrans)
add_subdirectory(src)