// Copyright (c) 2023-2024 Wavelet Lab
//
// This work is dual-licensed under MIT and GPL 2.0.
// You can choose between one of them if you use this work.
//
// SPDX-License-Identifier: MIT OR GPL-2.0

#ifndef SI2C_H
#define SI2C_H

#if defined(__EMSCRIPTEN__)
#include <stdint.h>
#include <errno.h>
#else
#include <linux/types.h>
#include <linux/errno.h>
#endif

#define MAKE_I2C_CMD(RD, RDZSZ, WRSZ, DEVNO, DATA)  (\
    (((RD) & 1U) << 31) | \
    (((RDZSZ) & 7U) << 28) | \
    (((WRSZ) & 3U) << 26) | \
    (((DEVNO) & 3U) << 24) | \
    (((DATA) & 0xffffffu) << 0))

int si2c_make_ctrl_reg(unsigned char idx,
        const unsigned char* dd,
        unsigned memoutsz,
        unsigned meminsz,
        unsigned* ctrl_w);

struct i2c_cache {
    unsigned busn : 1;
    unsigned addr : 7;
    unsigned lrui : 8;
};

unsigned si2c_update_lut_idx(struct i2c_cache* cd, unsigned addr, unsigned busno);
unsigned si2c_get_lut(const struct i2c_cache* cd);


#endif
