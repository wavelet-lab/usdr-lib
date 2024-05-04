// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include "gpio.h"

enum {
    GPIO_OUT_REG = 0,
    GPIO_BIT_SET = 1,
    GPIO_BIT_CLR = 2,
    GPIO_CFG_REG = 3,

    GPIO_CMD_OFF = 30,
    GPIO_CFG_IDX_OFF = 26,
    GPIO_CFG_IDX_MASK = 0xf,

    GPIO_CFG_VAL_MASK = 0x3,

    GPIO_CMD_MASK_OFF = 15,
    GPIO_CMD_MASK_MASK = 0x7fff,
};

int gpio_config(lldev_t lldev,
                subdev_t subdev,
                unsigned base,
                unsigned portno,
                unsigned cfg)
{
    uint32_t val;
    val = (GPIO_CFG_REG << GPIO_CMD_OFF) |
            ((portno & GPIO_CFG_IDX_MASK) << GPIO_CFG_IDX_OFF) |
            (cfg & GPIO_CFG_VAL_MASK);

    return lowlevel_reg_wr32(lldev, subdev, base, val);
}


int gpio_cmd(lldev_t lldev,
             subdev_t subdev,
             unsigned base,
             unsigned cmd,
             unsigned mask,
             unsigned value)
{
    if (cmd > 2)
        return -EINVAL;

    uint32_t val;
    val = (cmd << GPIO_CMD_OFF) |
            ((mask & GPIO_CMD_MASK_MASK) << GPIO_CMD_MASK_OFF) |
            (value & GPIO_CMD_MASK_MASK);

    return lowlevel_reg_wr32(lldev, subdev, base, val);
}

int gpio_in(lldev_t lldev,
             subdev_t subdev,
             unsigned base,
             unsigned *value)
{
    return lowlevel_reg_rd32(lldev, subdev, base, value);
}
