// Copyright (c) 2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef ADF4002B_H
#define ADF4002B_H

#include <usdr_lowlevel.h>

int adf4002b_reg_set(lldev_t dev, subdev_t subdev, lsopaddr_t ls_op_addr,
                     uint8_t reg, uint32_t value);

#endif
