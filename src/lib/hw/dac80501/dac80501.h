// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef DAC80501_H
#define DAC80501_H

#include <usdr_lowlevel.h>

enum dac80501_cfg {
    DAC80501_CFG_REF_DIV_GAIN_MUL = 0,
};

typedef enum dac80501_cfg dac80501_cfg_t;

int dac80501_init(lldev_t dev, subdev_t subdev, lsopaddr_t ls_op_addr,
                  dac80501_cfg_t config);

int dac80501_dac_set(lldev_t dev, subdev_t subdev, lsopaddr_t ls_op_addr,
                     uint16_t code);
int dac80501_dac_get(lldev_t dev, subdev_t subdev, lsopaddr_t ls_op_addr,
                     uint16_t* pcode);

#endif
