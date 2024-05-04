// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef DEV_PARAM_H
#define DEV_PARAM_H

#include <usdr_port.h>

struct opt_param_u32 {
    unsigned value;
    bool set;
};
typedef struct opt_param_u32 opt_u32_t;

#define BIT(x) (1u << (x))

static inline void opt_u32_set_null(opt_u32_t* p)
{
    p->set = false;
    p->value = 0;
}

static inline void opt_u32_set_val(opt_u32_t* p, unsigned val)
{
    p->set = true;
    p->value = val;
}

struct freq_auto_band_map
{
    unsigned stop_freq;
    uint8_t band;
    uint8_t sw   : 4;
    uint8_t swlb : 4;
    char name0[5];
    char name1[5];
};
typedef struct freq_auto_band_map freq_auto_band_map_t;


#endif
