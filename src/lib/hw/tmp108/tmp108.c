// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include "tmp108.h"

int tmp108_temp_get(lldev_t dev, subdev_t subdev, lsopaddr_t ls_op_addr,
                    int* outtemp)
{
    uint8_t taddr = 0;
    int16_t value;
    int res = lowlevel_get_ops(dev)->ls_op(dev, subdev,
                                           USDR_LSOP_I2C_DEV, ls_op_addr,
                                           2, &value, 1, &taddr);
    if (res)
        return res;

    *outtemp = value;
    return 0;
}

