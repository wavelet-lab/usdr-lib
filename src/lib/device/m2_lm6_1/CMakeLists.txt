# Copyright (c) 2023-2024 Wavelet Lab
# SPDX-License-Identifier: MIT

set(USDR_D_LIB_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/m2_lm6_1.c
    ${CMAKE_CURRENT_SOURCE_DIR}/usdr_ctrl.c
#    ${CMAKE_CURRENT_SOURCE_DIR}/device.h
)

list(APPEND USDR_LIBRARY_FILES ${USDR_D_LIB_FILES})
set(USDR_LIBRARY_FILES ${USDR_LIBRARY_FILES} PARENT_SCOPE)
