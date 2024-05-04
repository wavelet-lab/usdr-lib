# Copyright (c) 2023-2024 Wavelet Lab
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.8)
project(usdr_lib C)

# Auto generating registor descriptors
list(APPEND USDR_LIBRARY_FILES "")
list(APPEND USDR_INCLUDE_FILES "")
list(APPEND USDR_LINK_FILES    pthread rt)
list(APPEND USDR_DEPEND_TARGETS "")

include_directories(port)
include_directories(lowlevel)

add_subdirectory(port)
add_subdirectory(common)
add_subdirectory(lowlevel)
add_subdirectory(hw)
add_subdirectory(ipblks)
add_subdirectory(device)
add_subdirectory(models)
add_subdirectory(xdsp)
add_subdirectory(cal)

message("INC - ${USDR_INCLUDE_FILES}")
message("LNK - ${USDR_LINK_FILES}")
message("DEP - ${USDR_DEPEND_TARGETS}")

include_directories(${USDR_INCLUDE_FILES})
include_directories(${USDR_INCLUDE_DIRS})

add_library(usdr SHARED ${USDR_LIBRARY_FILES})
target_link_libraries(usdr pthread rt ${USDR_LINK_FILES} usdr-dsp)
add_dependencies(usdr ${USDR_DEPEND_TARGETS})
target_compile_options(usdr PRIVATE "-Wall" "-Werror=implicit-function-declaration")

set_target_properties(usdr PROPERTIES SOVERSION ${USDR_ABI_VERSION})
set_target_properties(usdr PROPERTIES VERSION ${USDR_LIBVER})

install(TARGETS usdr LIBRARY)
