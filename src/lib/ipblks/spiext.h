// Copyright (c) 2023-2024 Wavelet Lab
//
// This work is dual-licensed under MIT and GPL 2.0.
// You can choose between one of them if you use this work.
//
// SPDX-License-Identifier: MIT OR GPL-2.0

#ifndef SPIEXT_H
#define SPIEXT_H

#if defined(__EMSCRIPTEN__)
#include <stdint.h>
#include <errno.h>
typedef uint8_t __u8;
#else
#include <linux/types.h>
#include <linux/errno.h>
#endif

#define MAKE_SPIEXT_CFG(b, cs, div) ((((b) & 3) << 14) | (((cs) & 7) << 8) | ((div) & 0xff))
#define MAKE_SPIEXT_LSOPADR(cfg, eaddr, baddr) (\
    ((cfg) << 16) | (((eaddr) & 0xff) << 8) | ((baddr) & 0xff))


#define SPIEXT_LSOP_GET_CSR(x)  (((x) >> 8) & 0xff)
#define SPIEXT_LSOP_GET_CFG(x)   ((x) >> 16)
#define SPIEXT_LSOP_GET_BUS(x)   ((x) & 0xff)

unsigned spiext_make_data_reg(unsigned memoutsz,
                              const void* pout);

void spiext_parse_data_reg(unsigned spin,
                           unsigned meminsz,
                           void* pin);

#endif
