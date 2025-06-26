// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include "tca9555.h"
#include "def_tca9555.h"

#include <usdr_logging.h>

int tca9555_reg8_set(lldev_t dev, subdev_t subdev, lsopaddr_t ls_op_addr,
                      uint8_t reg, uint8_t value)
{
    uint8_t data[2] = { reg, value };
    return lowlevel_ls_op(dev, subdev,
                          USDR_LSOP_I2C_DEV, ls_op_addr,
                          0, NULL, 2, data);
}

int tca9555_reg8_get(lldev_t dev, subdev_t subdev, lsopaddr_t ls_op_addr,
                      uint8_t reg, uint8_t* oval)
{
    return lowlevel_ls_op(dev, subdev,
                          USDR_LSOP_I2C_DEV, ls_op_addr,
                          1, oval, 1, &reg);
}

int tca9555_reg16_set(lldev_t dev, subdev_t subdev, lsopaddr_t ls_op_addr,
                       uint8_t reg, uint16_t value)
{
    uint8_t data[3] = { reg, value, value >> 8 };
    return lowlevel_ls_op(dev, subdev,
                          USDR_LSOP_I2C_DEV, ls_op_addr,
                          0, NULL, 3, data);
}

int tca9555_reg16_get(lldev_t dev, subdev_t subdev, lsopaddr_t ls_op_addr,
                       uint8_t reg, uint16_t* oval)
{
    uint8_t data[1] = { reg };
    uint8_t odata[2] = { 0x00, 0x00 };
    int res = lowlevel_ls_op(dev, subdev,
                             USDR_LSOP_I2C_DEV, ls_op_addr,
                             2, odata, 1, data);

    *oval = (((unsigned)odata[0]) << 8) | odata[1];
    return res;
}
