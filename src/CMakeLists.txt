# Copyright (c) 2023-2024 Wavelet Lab
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.8)
project(usdr-world C)

include(GNUInstallDirs)

if(POLICY CMP0148)
    cmake_policy(SET CMP0148 OLD) #PythonInterp
endif()


file(READ "${PROJECT_SOURCE_DIR}/Changelog.txt" changelog_txt)
string(REGEX MATCH "Release ([0-9]+\\.[0-9]+\\.[0-9]+) \\(" CHANGELOG_MATCH "${changelog_txt}")
if(NOT CHANGELOG_MATCH)
    message(FATAL_ERROR "Failed to extract version number from Changelog.txt")
endif(NOT CHANGELOG_MATCH)
set(USDR_LIBVER "${CMAKE_MATCH_1}")
set(USDR_VERSION "${USDR_LIBVER}")
set(USDR_ABI_VERSION "0")


include(FeatureSummary)
include("cmake/set_default_buildtype.cmake")
include("cmake/detect_arch.cmake")
include("hwparser/hwparser.cmake")

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_CURRENT_SOURCE_DIR}/cmake/")
find_package(LIBUSB_1 REQUIRED)

option(BUILD_SHARED_LIBS "Build shared libraries(otherwise static libraries will be produced)" ON)
option(ENABLE_VERILATOR "Enable verilator lowlevel bridge" OFF)
option(ENABLE_SOAPY "Enable SopaySDR support" ON)
option(ENABLE_GUI  "Enable USDR QT5 simple GUI" ON)
option(ENABLE_TESTS  "Enable tests" OFF)


add_feature_info(VerilatorBridge ENABLE_VERILATOR "Verilator support for emulated devices")
add_feature_info(SoapySDR ENABLE_SOAPY "SopaySDR plugin")
add_feature_info(UsdrGUI ENABLE_GUI "USDR QT5 GUI")
add_feature_info(LibUSB LIBUSB_1_FOUND "USB devices support")

if (BUILD_SHARED_LIBS)
    set(LIB_TYPE SHARED)
else(BUILD_SHARED_LIBS)
    set(LIB_TYPE STATIC)
    add_compile_options(-fPIC)
endif(BUILD_SHARED_LIBS)

if(ENABLE_VERILATOR)
    add_definitions(-DENABLE_VERILATOR)
endif(ENABLE_VERILATOR)

# GDB Version <10 can't parse parameters correctly
add_compile_options(
    "$<$<CONFIG:DEBUG>:-O0;-g>"
    "$<$<CONFIG:RELEASE>:-O3>"
)

add_subdirectory(lib)
add_subdirectory(tools)

if(ENABLE_TESTS)
    add_subdirectory(tests)
    add_subdirectory(utests)
endif(ENABLE_TESTS)

if(ENABLE_SOAPY)
    add_subdirectory(soapysdr)
endif(ENABLE_SOAPY)

if(ENABLE_VERILATOR)
    add_subdirectory(verilator)
endif(ENABLE_VERILATOR)



include(FeatureSummary)
message(STATUS "")
message(STATUS "######################################################")
message(STATUS "## ${PROJECT_NAME} enabled features")
message(STATUS "######################################################")
feature_summary(WHAT ENABLED_FEATURES)
message(STATUS "######################################################")
message(STATUS "## ${PROJECT_NAME} disabled features")
message(STATUS "######################################################")
feature_summary(WHAT DISABLED_FEATURES)
message(STATUS "usdr version: v${USDR_VERSION}")
message(STATUS "Install prefix: ${CMAKE_INSTALL_PREFIX}")
message(STATUS "Install libs: ${CMAKE_INSTALL_LIBDIR}")
message(STATUS "Lib type: ${LIB_TYPE}")
message(STATUS "######################################################")
