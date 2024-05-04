// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef CLOCK_GEN_H
#define CLOCK_GEN_H

#include <stdint.h>

struct pll_vals {
    unsigned nint;
    uint64_t nfrac;
};
typedef struct pll_vals pll_vals_t;

struct vco_range {
    uint64_t vcomin;
    uint64_t vcomax;
};
typedef struct vco_range vco_range_t;

struct div_range {
    unsigned mindiv;
    unsigned maxdiv;
    unsigned step;
    unsigned count;    // number of div devices in the group, e.g. number of VCO prescalers you can choise from the next stage
    const unsigned *pinvlid; // pointer to sorted array of invalid dividers if any
};
typedef struct div_range div_range_t;

pll_vals_t pll_solver(uint64_t vco, uint32_t ref, uint64_t den);


int find_best_vco(const vco_range_t* pvcos, unsigned vco_count,
                  const div_range_t* div, const unsigned div_cascade_count,
                  const uint64_t *freq_req, unsigned *out_freq_divs, const unsigned freq_count);


uint64_t calc_serder_clock(uint64_t samplerte, unsigned f);
uint32_t calc_lmfc_clock(uint64_t samplerte, unsigned k);





#endif
