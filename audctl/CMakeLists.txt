# audctl/CMakeLists.txt

# Copyright (c) 2025 Omar Berrow

set(AUDCTL_SOURCES "audctl.c")

add_executable(audctl ${AUDCTL_SOURCES})

target_link_libraries(audctl PRIVATE autrans_so)

install(TARGETS audctl)