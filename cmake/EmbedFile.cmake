# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright (C) 2026 Tom Needham
# Writes INPUT as a NUL-terminated C byte array named NAME into OUTPUT, so the
# library carries the file without reading it at run time.
#   cmake -DINPUT=file -DOUTPUT=header.h -DNAME=symbol -P EmbedFile.cmake
file(READ "${INPUT}" content HEX)
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," bytes "${content}")
string(REGEX REPLACE "((0x[0-9a-f][0-9a-f],){16})" "\\1\n" bytes "${bytes}")
get_filename_component(source "${INPUT}" NAME)
file(WRITE "${OUTPUT}.tmp"
    "/* Generated from ${source} by cmake/EmbedFile.cmake. */\n"
    "static const unsigned char ${NAME}[] = {\n${bytes}0x00};\n")
# Rewrite only on change, so dependents rebuild only when the file changes.
execute_process(COMMAND ${CMAKE_COMMAND} -E copy_if_different "${OUTPUT}.tmp" "${OUTPUT}")
file(REMOVE "${OUTPUT}.tmp")
