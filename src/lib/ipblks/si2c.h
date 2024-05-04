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
#else
#include <linux/types.h>
#endif

#define MAKE_I2C_CMD(RD, RDZSZ, WRSZ, DEVNO, DATA)  (\
    (((RD) & 1U) << 31) | \
    (((RDZSZ) & 7U) << 28) | \
    (((WRSZ) & 3U) << 26) | \
    (((DEVNO) & 3U) << 24) | \
    (((DATA) & 0xffffffu) << 0))

int si2c_make_ctrl_reg(
        uint8_t bus,
        const uint8_t* dd,
        unsigned memoutsz,
        unsigned meminsz,
        uint32_t* ctrl_w);

#endif
