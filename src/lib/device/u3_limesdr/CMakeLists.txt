# Copyright (c) 2023-2024 Wavelet Lab
# SPDX-License-Identifier: MIT


set(U3_LIME_LIB_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/u3_limesdr.c
    ${CMAKE_CURRENT_SOURCE_DIR}/limesdr_ctrl.c
    ${CMAKE_CURRENT_SOURCE_DIR}/limesdr_fpga.c
)

list(APPEND USDR_LIBRARY_FILES ${U3_LIME_LIB_FILES})
set(USDR_LIBRARY_FILES ${USDR_LIBRARY_FILES} PARENT_SCOPE)
