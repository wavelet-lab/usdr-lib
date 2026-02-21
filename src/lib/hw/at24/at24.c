// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include "at24.h"

#include <usdr_logging.h>

// int at24_1addr_mem_set(lldev_t dev, subdev_t subdev, lsopaddr_t ls_op_addr, uint8_t addr, unsigned size, const uint8_t *pdata);
int at24_saddr_mem_get(lldev_t dev, subdev_t subdev, lsopaddr_t ls_op_addr, uint8_t addr, unsigned size, uint8_t *pdata)
{
    int res = 0;
    for (unsigned off = 0; off < size;) {
        uint8_t odata[4] = { 0xff, 0xff, 0xff, 0xff };
        unsigned rem = size - off;
        if (rem > 4)
            rem = 4;

        uint8_t byte_addr = addr + off;
        res = res ? res : lowlevel_ls_op(dev, subdev, USDR_LSOP_I2C_DEV, ls_op_addr, rem, odata, 1, &byte_addr);

        for (unsigned j = 0; j < rem; j++)
            pdata[off + j] = odata[j];

        off += rem;
    }

    return res;
}

