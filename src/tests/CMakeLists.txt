# Copyright (c) 2023-2024 Wavelet Lab
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.8)
project(usdr_tests C)

include_directories(../lib/port)
include_directories(../lib/lowlevel)
include_directories(../lib/models)

add_executable(usdr_dm_calibrate usdr_dm_calibrate.c simpleapi.c)
target_link_libraries(usdr_dm_calibrate usdr)

add_executable(usdr_simple_api_test usdr_simple_api_test.c simpleapi.c)
target_link_libraries(usdr_simple_api_test usdr)

add_executable(lowlevel_lvds_perf lowlevel_lvds_perf.c)
target_link_libraries(lowlevel_lvds_perf usdr)

