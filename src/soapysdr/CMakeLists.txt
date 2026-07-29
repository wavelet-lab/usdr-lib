# Copyright (c) 2023-2024 Wavelet Lab
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.8)
set(CMAKE_CXX_STANDARD 11)
project(usdr-soapy C CXX)

find_package(SoapySDR REQUIRED)

include_directories(../lib/port)
include_directories(../lib/models)

# WARN: Explicit DESTINATION must be removed when 20.04 support is dropped!
SOAPY_SDR_MODULE_UTIL(
    TARGET
        USDRSupport
    DESTINATION
        ${CMAKE_INSTALL_LIBDIR}/SoapySDR/modules${SOAPY_SDR_ABI_VERSION}/
    SOURCES
        usdr_soapy.cpp usdr_soapy_reg.cpp rx_packet_buffer.cpp
    LIBRARIES
        usdr
)

if(ENABLE_TESTS)
    add_executable(test_usdr_soapy tests/test_usdr_soapy.c)
    target_link_libraries(test_usdr_soapy ${SoapySDR_LIBRARIES})
    set_target_properties(test_usdr_soapy PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/tests
    )

    add_executable(test_rx_packet_buffer tests/test_rx_packet_buffer.cpp rx_packet_buffer.cpp)
    target_link_libraries(test_rx_packet_buffer usdr)
    set_target_properties(test_rx_packet_buffer PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/tests
    )
    add_test(NAME rx_packet_buffer COMMAND $<TARGET_FILE:test_rx_packet_buffer>)
endif(ENABLE_TESTS)

option(ENABLE_SOAPY_HIL_TESTS "Enable SoapySDR hardware-in-the-loop tests" OFF)
if(ENABLE_TESTS AND ENABLE_SOAPY_HIL_TESTS)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    add_test(
        NAME soapy_usdr_hil
        COMMAND ${Python3_EXECUTABLE}
            ${CMAKE_CURRENT_SOURCE_DIR}/tests/soapy_usdr_hil.py
            --require-device
            --device driver=usdr
    )
endif()
