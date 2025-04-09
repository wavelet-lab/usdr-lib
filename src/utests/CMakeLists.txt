# Copyright (c) 2023-2024 Wavelet Lab
# SPDX-License-Identifier: MIT

include_directories(../lib/port)
include_directories(../lib/lowlevel)
include_directories(../lib/hw)

add_library(mock_lowlevel STATIC mock_lowlevel.c)

set(TEST_SUIT_SRCS
    test_suite.c
    ring_buffer_test.c
    trig_test.c
    clockgen_test.c
    lmk05318_solver_test.c
    lmx2820_solver_test.c
    lmx1214_solver_test.c
)

include_directories(../lib/xdsp)
include_directories(../lib/common)

add_executable(usdr_testsuit ${TEST_SUIT_SRCS})
target_link_libraries(usdr_testsuit usdr mock_lowlevel usdr-dsp check subunit m rt pthread)
