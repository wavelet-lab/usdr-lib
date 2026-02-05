# Copyright (c) 2023-2024 Wavelet Lab
# SPDX-License-Identifier: MIT

set(USDR_PORT_LIB_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/usdr_logging.c
    ${CMAKE_CURRENT_SOURCE_DIR}/usdr_port.c
    ${CMAKE_CURRENT_SOURCE_DIR}/usdr_helpers.c
    ${CMAKE_CURRENT_SOURCE_DIR}/portable_fnmatch.c
)

list(APPEND USDR_LIBRARY_FILES ${USDR_PORT_LIB_FILES})
set(USDR_LIBRARY_FILES ${USDR_LIBRARY_FILES} PARENT_SCOPE)

if(NOT EMSCRIPTEN)
    add_executable(test_fnmatch test_fnmatch.c portable_fnmatch.c)
endif()

install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/ DESTINATION include/usdr FILES_MATCHING PATTERN "*.h")
