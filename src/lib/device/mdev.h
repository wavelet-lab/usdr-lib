// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef MDEV_H
#define MDEV_H

#include <usdr_port.h>
#include <usdr_logging.h>
#include <usdr_lowlevel.h>

int mdev_create(unsigned pcnt, const char** names, const char** values, lldev_t* odev,
                unsigned idx, char** bus_names, unsigned bus_cnt);

#endif
