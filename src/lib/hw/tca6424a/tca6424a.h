// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef TCA6424A_H
#define TCA6424A_H

#include <usdr_lowlevel.h>

enum tca6424a_regs {
    TCA6424_IN0 = 0,
    TCA6424_OUT0 = 4,
    TCA6424_INV0 = 8,
    TCA6424_CFG0 = 12, // 0 - Output, 1 - Input/Hi-Z
};

int tca6424a_reg8_set(lldev_t dev, subdev_t subdev, lsopaddr_t ls_op_addr,
                      uint8_t reg, uint8_t value);
int tca6424a_reg8_get(lldev_t dev, subdev_t subdev, lsopaddr_t ls_op_addr,
                      uint8_t reg, uint8_t* oval);

int tca6424a_reg16_set(lldev_t dev, subdev_t subdev, lsopaddr_t ls_op_addr,
                      uint8_t reg, uint16_t value);
int tca6424a_reg16_get(lldev_t dev, subdev_t subdev, lsopaddr_t ls_op_addr,
                      uint8_t reg, uint16_t* oval);

// int tca6424a_reg24_set(lldev_t dev, subdev_t subdev, lsopaddr_t ls_op_addr,
//                        uint8_t reg, uint32_t value);
int tca6424a_reg24_get(lldev_t dev, subdev_t subdev, lsopaddr_t ls_op_addr,
                       uint8_t reg, uint32_t* oval);

#endif
