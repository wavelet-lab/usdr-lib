# Copyright (c) 2023-2024 Wavelet Lab
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.8)
project(usdr_tools C)

include_directories(../lib/port)
include_directories(../lib/lowlevel)
include_directories(../lib/xdsp)

add_executable(usdr_flash usdr_flash.c)
target_link_libraries(usdr_flash usdr)
install(TARGETS usdr_flash RUNTIME)

include_directories(../lib/models)

add_executable(usdr_dm_create usdr_dm_create.c)
target_link_libraries(usdr_dm_create usdr)
install(TARGETS usdr_dm_create RUNTIME)

add_executable(usdr_dm_sensors usdr_dm_sensors.c)
target_link_libraries(usdr_dm_sensors usdr)
install(TARGETS usdr_dm_sensors RUNTIME)

add_executable(usdr_lml7_test lms7002_dm_limelight.c)
target_link_libraries(usdr_lml7_test usdr)

add_executable(usdr_dm_gpsdo usdr_dm_gpsdo.c)
target_link_libraries(usdr_dm_gpsdo usdr)
install(TARGETS usdr_dm_gpsdo RUNTIME)

list(APPEND WVLT_SCRIPTS
    wvlt_sdr_wrapper.py
)
configure_file(${WVLT_SCRIPTS} . USE_SOURCE_PERMISSIONS COPYONLY)
