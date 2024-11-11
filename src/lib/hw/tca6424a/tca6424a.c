// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include "tca6424a.h"
#include "def_tca6424a.h"

#include <usdr_logging.h>

int tca6424a_reg8_set(lldev_t dev, subdev_t subdev, lsopaddr_t ls_op_addr,
                      uint8_t reg, uint8_t value)
{
    uint8_t data[2] = { reg, value };
    return lowlevel_ls_op(dev, subdev,
                          USDR_LSOP_I2C_DEV, ls_op_addr,
                          0, NULL, 2, data);
}

int tca6424a_reg8_get(lldev_t dev, subdev_t subdev, lsopaddr_t ls_op_addr,
                      uint8_t reg, uint8_t* oval)
{
    return lowlevel_ls_op(dev, subdev,
                          USDR_LSOP_I2C_DEV, ls_op_addr,
                          1, oval, 1, &reg);
}


int tca6424a_reg16_set(lldev_t dev, subdev_t subdev, lsopaddr_t ls_op_addr,
                      uint8_t reg, uint16_t value)
{
    uint8_t data[3] = { 0x80 | reg, value, value >> 8 };
    return lowlevel_ls_op(dev, subdev,
                          USDR_LSOP_I2C_DEV, ls_op_addr,
                          0, NULL, 3, data);
}

int tca6424a_reg16_get(lldev_t dev, subdev_t subdev, lsopaddr_t ls_op_addr,
                       uint8_t reg, uint16_t* oval)
{
    uint8_t data[1] = { 0x80 | reg };
    return lowlevel_ls_op(dev, subdev,
                          USDR_LSOP_I2C_DEV, ls_op_addr,
                          2, oval, 1, data);
}



// int tca6424a_reg24_set(lldev_t dev, subdev_t subdev, lsopaddr_t ls_op_addr,
//                        uint8_t reg, uint32_t value)
// {
//     uint8_t data[4] = { reg, value, value >> 8, value >> 16 };
//     return lowlevel_ls_op(dev, subdev,
//                           USDR_LSOP_I2C_DEV, ls_op_addr,
//                           0, NULL, 4, data);
// }

int tca6424a_reg24_get(lldev_t dev, subdev_t subdev, lsopaddr_t ls_op_addr,
                       uint8_t reg, uint32_t* oval)
{
    uint8_t data[1] = { 0x80 | reg };
    return lowlevel_ls_op(dev, subdev,
                          USDR_LSOP_I2C_DEV, ls_op_addr,
                          3, oval, 1, data);
}
