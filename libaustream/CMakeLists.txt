# libaustream/CMakeLists.txt

# Copyright (c) 2025 Omar Berrow

set(austream_SOURCES "stream.c")

add_library(austream_obj OBJECT ${austream_SOURCES})
set_property(TARGET austream_obj PROPERTY POSITION_INDEPENDENT_CODE 1)

if (ENABLE_SHARED)
    add_library(austream_so SHARED $<TARGET_OBJECTS:austream_obj>)
    set_target_properties(austream_so PROPERTIES OUTPUT_NAME austream)
    install(TARGETS austream_so)
endif()
if (ENABLE_STATIC)
    add_library(austream_sh STATIC $<TARGET_OBJECTS:austream_obj>)
    set_target_properties(austream_sh PROPERTIES OUTPUT_NAME austream)
    install(TARGETS austream_so)
endif()