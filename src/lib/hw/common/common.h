// Copyright (c) 2025 Wavelet Lab
// SPDX-License-Identifier: MIT
#ifndef HW_COMMON_H
#define HW_COMMON_H

#include "usdr_lowlevel.h"

struct common_hw_state_struct
{
    lldev_t  dev;
    unsigned subdev;
    unsigned lsaddr;
};
typedef struct common_hw_state_struct common_hw_state_struct_t;


int common_ti_calc_sync_delay(uint32_t clkpos, unsigned* calced_delay);
int common_print_registers_a8d16(uint32_t* regs, unsigned count, int loglevel);
int common_spi_post(void* o, uint32_t* regs, unsigned count);
int common_spi_get(void* o, uint16_t addr, uint16_t* out);

#endif // HW_COMMON_H
