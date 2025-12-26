# libautrans/CMakeLists.txt

# Copyright (c) 2025 Omar Berrow

set(AUTRANS_SOURCES "trans.c")

add_library(autrans_obj OBJECT ${AUTRANS_SOURCES})
set_property(TARGET autrans_obj PROPERTY POSITION_INDEPENDENT_CODE 1)

if (ENABLE_SHARED)
    add_library(autrans_so SHARED $<TARGET_OBJECTS:autrans_obj>)
    set_target_properties(autrans_so PROPERTIES OUTPUT_NAME autrans)
    install(TARGETS autrans_so)
endif()
if (ENABLE_STATIC)
    add_library(autrans_sh STATIC $<TARGET_OBJECTS:autrans_obj>)
    set_target_properties(autrans_sh PROPERTIES OUTPUT_NAME autrans)
    install(TARGETS autrans_sh)
endif()