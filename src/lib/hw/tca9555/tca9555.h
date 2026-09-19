// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef TCA9555_H
#define TCA9555_H

#include <usdr_lowlevel.h>

enum tca9555_regs {
    TCA9555_IN0 = 0,
    TCA9555_OUT0 = 2,
    TCA9555_INV0 = 4,
    TCA9555_CFG0 = 6, // 0 - Output, 1 - Input/Hi-Z
};

int tca9555_reg8_set(lldev_t dev, subdev_t subdev, lsopaddr_t ls_op_addr,
                      uint8_t reg, uint8_t value);
int tca9555_reg8_get(lldev_t dev, subdev_t subdev, lsopaddr_t ls_op_addr,
                      uint8_t reg, uint8_t* oval);

int tca9555_reg16_set(lldev_t dev, subdev_t subdev, lsopaddr_t ls_op_addr,
                       uint8_t reg, uint16_t value);
int tca9555_reg16_get(lldev_t dev, subdev_t subdev, lsopaddr_t ls_op_addr,
                       uint8_t reg, uint16_t* oval);


#endif
