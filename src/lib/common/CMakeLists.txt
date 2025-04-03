# Copyright (c) 2023-2024 Wavelet Lab
# SPDX-License-Identifier: MIT

set(USDR_COMMON_LIB_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/ring_buffer.c
    ${CMAKE_CURRENT_SOURCE_DIR}/clock_gen.c
    ${CMAKE_CURRENT_SOURCE_DIR}/parse_params.c
    ${CMAKE_CURRENT_SOURCE_DIR}/ring_circbuf.c
)


list(APPEND USDR_LIBRARY_FILES ${USDR_COMMON_LIB_FILES})
set(USDR_LIBRARY_FILES ${USDR_LIBRARY_FILES} PARENT_SCOPE)

install(FILE ${CMAKE_CURRENT_SOURCE_DIR}/ring_buffer.h DESTINATION include/usdr)
