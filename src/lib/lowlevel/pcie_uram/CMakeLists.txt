# Copyright (c) 2023-2024 Wavelet Lab
# SPDX-License-Identifier: MIT

# static build

set(USDR_PCIE_URAM_LIB_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/pcie_uram_main.c
    ${CMAKE_CURRENT_SOURCE_DIR}/pcie_uram_main.h
)

list(APPEND USDR_LIBRARY_FILES ${USDR_PCIE_URAM_LIB_FILES})
set(USDR_LIBRARY_FILES ${USDR_LIBRARY_FILES} PARENT_SCOPE)
