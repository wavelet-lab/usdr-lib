// Copyright (c) 2025 Wavelet Lab
// SPDX-License-Identifier: MIT

#include <stdint.h>
#include <inttypes.h>
#include <assert.h>

#include "lmx2820.h"
#include "def_lmx2820.h"
#include <usdr_logging.h>

enum {
    OUT_FREQ_MIN =    45000000ul,
    OUT_FREQ_MAX = 22600000000ul,

    VCO_MIN =  5650000000ul,
    VCO_MAX = 11300000000ul,

    PLL_R_PRE_DIV_MIN = 1,
    PLL_R_PRE_DIV_MAX = 4095,

    MULT_MIN = 3,
    MULT_MAX = 7,

    PLL_R_DIV_MIN = 1,
    PLL_R_DIV_MAX = 255,

    OUT_DIV_LOG2_MIN = 1,
    OUT_DIV_LOG2_MAX = 7,
};

enum {
    VCO_CORE1 = 0,
    VCO_CORE2,
    VCO_CORE3,
    VCO_CORE4,
    VCO_CORE5,
    VCO_CORE6,
    VCO_CORE7,
};

enum {
    MASH_ORDER0 = 0,
    MASH_ORDER1,
    MASH_ORDER2,
    MASH_ORDER3,
};

struct vco_core {
    uint64_t fmin, fmax;
    unsigned ndiv_min[MASH_ORDER3 + 1];
};
typedef struct vco_core vco_core_t;

static vco_core_t vco_cores[VCO_CORE7 + 1] =
{
    {VCO_MIN,        6350000000ul, {12,18,19,24}},
    { 6350000000ul,  7300000000ul, {14,21,22,26}},
    { 7300000000ul,  8100000000ul, {16,23,24,26}},
    { 8100000000ul,  9000000000ul, {16,26,27,29}},
    { 9000000000ul,  9800000000ul, {18,28,29,31}},
    { 9800000000ul, 10600000000ul, {18,30,31,33}},
    {10600000000ul, (VCO_MAX + 1), {20,33,34,36}}
};

static int lmx2820_get_worst_vco_core(uint64_t vco_freq, unsigned mash_order, unsigned* vco_core, unsigned* ndiv_min)
{
    if(!vco_core || !ndiv_min ||
        vco_freq < VCO_MIN || vco_freq > OUT_FREQ_MAX ||
        mash_order > MASH_ORDER3)
    {
        return -EINVAL;
    }

    for(unsigned i = 0; i <= VCO_CORE7; ++i)
    {
        const vco_core_t r = vco_cores[i];
        if(vco_freq >= r.fmin && vco_freq < r.fmax)
        {
            *vco_core = i;
            *ndiv_min = r.ndiv_min[mash_order];
            return 0;
        }
    }

    assert(1); // should never reach this line
    return -EINVAL;
}


