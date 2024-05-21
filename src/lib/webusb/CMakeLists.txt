# Copyright (c) 2023-2024 Wavelet Lab
# SPDX-License-Identifier: MIT

# WebUSB Firmware files
cmake_minimum_required(VERSION 3.8)

include("../../cmake/set_default_buildtype.cmake")

project(webusb_test C)

include("../../cmake/detect_arch.cmake")

set(WVLT_WEBUSB_BUILD 1 CACHE BOOL "Webusb gcc debug build")
add_definitions(-DWVLT_WEBUSB_BUILD)

option(ENABLE_VERILATOR "Enable verilator lowlevel bridge" OFF)
#option(CROSS_ARM    "Enable cross compilation for ARM (UGLY UGLY, fix me please!)" OFF)

add_compile_options(
    "$<$<CONFIG:DEBUG>:-O0;-g>"
    "$<$<CONFIG:RELEASE>:-O3>"
)

if(ENABLE_VERILATOR)
    add_definitions(-DENABLE_VERILATOR)
endif(ENABLE_VERILATOR)

include(../../hwparser/hwparser.cmake)

# Auto generating registor descriptors
list(APPEND USDR_LIBRARY_FILES "")
list(APPEND USDR_INCLUDE_FILES "")
list(APPEND USDR_DEPEND_TARGETS "")

include_directories(sysroot/include)
include_directories(../port)
include_directories(../lowlevel)

add_subdirectory(../port port)
add_subdirectory(../common common)
add_subdirectory(../hw hw)
add_subdirectory(../ipblks ipblks)
add_subdirectory(../device device)
add_subdirectory(../models models)
add_subdirectory(../xdsp xdsp)
add_subdirectory(../cal cal)

set(WEBUSB_TEST_LIB_FILES
    "../lowlevel/usb_uram/usb_uram_webusb.c"
    "../lowlevel/usb_ft601/usb_ft601_webusb.c"
    "../lowlevel/usdr_lowlevel.c"
    "controller_xsdr.c"
    "controller_usdr.c"
    "controller_ugen.c"
    "controller_lime.c"
    "controller.c"
    "webusb.c"
    )

set(EXTRA_FILES
    "tiny-json.c"
    "base64.c"
    )

include_directories(${USDR_INCLUDE_FILES})
include_directories(${USDR_INCLUDE_DIRS})

if(WVLT_ARCH_X86 OR WVLT_ARCH_X86_64)
    set_source_files_properties(${XDSPLIB_FILES} PROPERTIES COMPILE_FLAGS "-msse4.1")
elseif(WVLT_ARCH_ARM64)
    #set_source_files_properties(${XDSPLIB_FILES} PROPERTIES COMPILE_FLAGS "-mfpu=neon")
endif()

add_library(webusb_test SHARED ${USDR_LIBRARY_FILES} ${WEBUSB_TEST_LIB_FILES} ${EXTRA_FILES} ${XDSPLIB_FILES})
target_link_libraries(webusb_test m pthread rt)
add_dependencies(webusb_test ${USDR_DEPEND_TARGETS})
target_compile_options(webusb_test PRIVATE "-Wall" "-Werror=implicit-function-declaration")

add_executable(test_webusb_wrapper test_webusb_wrapper.c)
target_link_libraries(test_webusb_wrapper webusb_test)

add_executable(test_webusb_libusb test_webusb_libusb.c)
target_link_libraries(test_webusb_libusb webusb_test usb-1.0)
