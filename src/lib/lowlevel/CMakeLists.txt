# Copyright (c) 2023-2024 Wavelet Lab
# SPDX-License-Identifier: MIT

set(USDR_LOWLEVEL_LIB_FILES
    ${CMAKE_CURRENT_SOURCE_DIR}/usdr_lowlevel.c
    ${CMAKE_CURRENT_SOURCE_DIR}/usdr_lowlevel.h
)

if(NOT EMSCRIPTEN)
    list(APPEND USDR_LOWLEVEL_LIB_FILES
            ${CMAKE_CURRENT_SOURCE_DIR}/libusb_generic.c
            ${CMAKE_CURRENT_SOURCE_DIR}/libusb_generic.h
    )
    add_subdirectory(pcie_uram)
endif()

add_subdirectory(usb_uram)
add_subdirectory(usb_ft601)

if(ENABLE_VERILATOR)
    add_subdirectory(verilator_ll)
endif(ENABLE_VERILATOR)

list(APPEND USDR_LIBRARY_FILES ${USDR_LOWLEVEL_LIB_FILES})

set(USDR_LIBRARY_FILES ${USDR_LIBRARY_FILES} PARENT_SCOPE)
set(USDR_INCLUDE_FILES ${USDR_INCLUDE_FILES} PARENT_SCOPE)
set(USDR_LINK_FILES ${USDR_LINK_FILES} PARENT_SCOPE)
