# Copyright (c) 2023-2024 Wavelet Lab
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.8)

include("../../../cmake/set_default_buildtype.cmake")

set(CMAKE_CXX_COMPILER "em++")
project(control CXX)

if(NOT EMSCRIPTEN)
    message(FATAL_ERROR "You should configure for Emscripten build! (emcmake cmake ..)")
endif()

include("../../../cmake/detect_arch.cmake")

option(ENABLE_VERILATOR "Enable verilator lowlevel bridge" OFF)
#option(CROSS_ARM    "Enable cross compilation for ARM (UGLY UGLY, fix me please!)" OFF)

add_compile_options(
    #"-Wall"
    #"-Wno-unused-variable" "-Wno-unused-function" "-Wno-vla-cxx-extension"
    "-Wno-c++11-narrowing" "-Wno-deprecated" "-Wno-c99-designator"
    "$<$<CONFIG:DEBUG>:-O0;-gsource-map>"
    "$<$<CONFIG:RELEASE>:-O3>"
)

if(ENABLE_VERILATOR)
    add_definitions(-DENABLE_VERILATOR)
endif(ENABLE_VERILATOR)

include(../../../hwparser/hwparser.cmake)

# Auto generating registor descriptors
list(APPEND USDR_LIBRARY_FILES "")
list(APPEND USDR_INCLUDE_FILES "")
list(APPEND USDR_DEPEND_TARGETS "")

include_directories(..)
include_directories(sysroot/include)
include_directories(../../port)
include_directories(../../lowlevel)
include_directories(../../json_controller)

add_subdirectory(../../port port)
add_subdirectory(../../common common)
add_subdirectory(../../hw hw)
add_subdirectory(../../ipblks ipblks)
add_subdirectory(../../device device)
add_subdirectory(../../models models)
add_subdirectory(../../xdsp xdsp)
add_subdirectory(../../cal cal)
add_subdirectory(../../json_controller json_controller)

set(CONTROL_LIB_FILES
    "../../lowlevel/usb_uram/usb_uram_generic.c"
    "../../lowlevel/usb_uram/usb_uram_webusb.c"
    "../../lowlevel/usb_ft601/usb_ft601_generic.c"
    "../../lowlevel/usb_ft601/usb_ft601_webusb.c"
    "../../lowlevel/usdr_lowlevel.c"
    "../webusb.c"
    "js_webusb.c"
    )

include_directories(${USDR_INCLUDE_FILES})
include_directories(${USDR_INCLUDE_DIRS})

set_source_files_properties(${USDR_LIBRARY_FILES} PROPERTIES LANGUAGE CXX)
set_source_files_properties(${CONTROL_LIB_FILES} PROPERTIES LANGUAGE CXX)
set_source_files_properties(${JSON_CONTROLLER_FILES} PROPERTIES LANGUAGE CXX)
set_source_files_properties(${XDSPLIB_FILES} PROPERTIES LANGUAGE CXX)

if(WVLT_ARCH_X86 OR WVLT_ARCH_X86_64)
    set_source_files_properties(${XDSPLIB_FILES} PROPERTIES COMPILE_FLAGS "-msse4.1 -msimd128")
elseif(WVLT_ARCH_ARM64)
    set_source_files_properties(${XDSPLIB_FILES} PROPERTIES COMPILE_FLAGS "-mfpu=neon -msimd128")
endif()

add_library(control STATIC ${USDR_LIBRARY_FILES} ${XDSPLIB_FILES} ${CONTROL_LIB_FILES} ${JSON_CONTROLLER_FILES})
add_dependencies(control ${USDR_DEPEND_TARGETS})
set_target_properties(control PROPERTIES LINKER_LANGUAGE CXX)
