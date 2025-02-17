# Copyright (c) 2023-2024 Wavelet Lab
# SPDX-License-Identifier: MIT

set(WVLT_ARCH ${CMAKE_HOST_SYSTEM_PROCESSOR} CACHE STRING "Arch being built for")

if(${WVLT_ARCH} STREQUAL "x86")
    add_definitions(-DWVLT_ARCH_X86)
    set(WVLT_ARCH_X86 1 CACHE BOOL "Arch x86")
    message(STATUS "Will compile for ${WVLT_ARCH} architecture")
    message(STATUS "Intel SIMD intrinsics will be used")

elseif(${WVLT_ARCH} STREQUAL "x86_64")
    add_definitions(-DWVLT_ARCH_X86_64)
    set(WVLT_ARCH_X86_64 1 CACHE BOOL "Arch x86_64")
    message(STATUS "Will compile for ${WVLT_ARCH} architecture")
    message(STATUS "Intel SIMD intrinsics will be used")

elseif(${WVLT_ARCH} STREQUAL "aarch64" OR ${WVLT_ARCH} STREQUAL "arm64")
    add_definitions(-DWVLT_ARCH_ARM64)
    set(WVLT_ARCH_ARM64 1 CACHE BOOL "Arch ARM64")
    message(STATUS "Will compile for ${WVLT_ARCH} architecture")
    message(STATUS "NEON SIMD intrinsics will be used")

else()
    add_definitions(-DWVLT_ARCH_GENERIC)
    set(WVLT_ARCH_GENERIC 1 CACHE BOOL "Arch generic")
    message(WARNING "Architecture <${WVLT_ARCH}> conditionally supported")
    message(WARNING "SIMD instructions will NOT be used, poor performance expected!")
endif()
