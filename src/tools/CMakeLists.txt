# Copyright (c) 2023-2024 Wavelet Lab
# SPDX-License-Identifier: MIT

cmake_minimum_required(VERSION 3.8)
project(usdr_tools C)

include_directories(../lib/port)
include_directories(../lib/lowlevel)

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
