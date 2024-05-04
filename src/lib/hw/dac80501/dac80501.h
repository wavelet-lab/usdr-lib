// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef DAC80501_H
#define DAC80501_H

#include <usdr_lowlevel.h>

typedef int (*dac80501_i2c_func_t)(lldev_t dev, subdev_t subdev, unsigned ls_op, lsopaddr_t ls_op_addr,
                                   size_t meminsz, void* pin, size_t memoutsz,
                                   const void* pout);

enum dac80501_cfg {
    DAC80501_CFG_REF_DIV_GAIN_MUL = 0,
};

typedef enum dac80501_cfg dac80501_cfg_t;

int dac80501_init(dac80501_i2c_func_t func, lldev_t dev, subdev_t subdev, lsopaddr_t ls_op_addr,
                  dac80501_cfg_t config);

int dac80501_dac_set(dac80501_i2c_func_t func, lldev_t dev, subdev_t subdev, lsopaddr_t ls_op_addr,
                     uint16_t code);
int dac80501_dac_get(dac80501_i2c_func_t func, lldev_t dev, subdev_t subdev, lsopaddr_t ls_op_addr,
                     uint16_t* pcode);

#endif
