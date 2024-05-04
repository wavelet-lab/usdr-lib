// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef SI549_H
#define SI549_H

#include <usdr_lowlevel.h>

int si549_set_freq(lldev_t dev, subdev_t subdev, lsopaddr_t addr,
                   unsigned output);

int si549_dump(lldev_t dev, subdev_t subdev, lsopaddr_t addr);

int si549_enable(lldev_t dev, subdev_t subdev, lsopaddr_t addr, bool enable);

#endif
