// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef AT24_H
#define AT24_H

#include <usdr_lowlevel.h>

enum {
    AT24_SECURE_SERIAL_OFF = 0x80,
    AT24_SECURE_USER_OFF = 0x90,
};

// single byte addres functions
int at24_saddr_mem_set(lldev_t dev, subdev_t subdev, lsopaddr_t ls_op_addr, uint8_t addr, unsigned size, const uint8_t *pdata);
int at24_saddr_mem_get(lldev_t dev, subdev_t subdev, lsopaddr_t ls_op_addr, uint8_t addr, unsigned size, uint8_t *pdata);

#endif
