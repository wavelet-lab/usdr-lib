# Copyright (c) 2023-2024 Wavelet Lab
# SPDX-License-Identifier: MIT

if(NOT DEFINED CACHE{CMAKE_BUILD_TYPE})
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Cmake build type")
endif()
message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
