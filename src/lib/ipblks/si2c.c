// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include <stdint.h>
#include "si2c.h"
#include <errno.h>

int si2c_make_ctrl_reg(
        uint8_t bus,
        const uint8_t* dd,
        unsigned memoutsz,
        unsigned meminsz,
        uint32_t* ctrl_w)
{
    uint32_t i2ccmd, data;

    if (memoutsz > 3)
        return -EINVAL;
    if (meminsz > 4)
        return -EINVAL;

    if (memoutsz == 1) {
        data = dd[0];
    } else if (memoutsz == 1) {
        data = dd[0] | (dd[1] << 8);
    } else {
        data = dd[0] | (dd[1] << 8) | (dd[2] << 16);
    }

    i2ccmd = MAKE_I2C_CMD(meminsz > 0, meminsz - 1, memoutsz, bus, data);
    *ctrl_w = i2ccmd;

    return 0;
}
