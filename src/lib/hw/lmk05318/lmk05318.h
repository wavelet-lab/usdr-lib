// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef LMK05318_H
#define LMK05318_H

#include <usdr_lowlevel.h>

struct lmk05318_state {
    lldev_t dev;
    unsigned subdev;
    unsigned lsaddr;

    // Ref deviders
    unsigned fref_pll2_div_rp; // 3 to 6
    unsigned fref_pll2_div_rs; // 1 - 32

    // VCO2 freq
    uint64_t vco2_freq;
};

enum lmk05318_type {
    LVDS,
    CML,
    LVPECL,
    LVCMOS,
    OUT_OFF,
};

typedef struct lmk05318_state lmk05318_state_t;

int lmk05318_create(lldev_t dev, unsigned subdev, unsigned lsaddr, unsigned flags, lmk05318_state_t* out);

int lmk05318_tune_apll2(lmk05318_state_t* d, uint32_t freq, unsigned *last_div);
int lmk05318_set_out_div(lmk05318_state_t* d, unsigned port, unsigned div);
int lmk05318_set_out_mux(lmk05318_state_t* d, unsigned port, bool pll1, unsigned otype);

enum lock_msk {
    LMK05318_LOS_XO = 1,
    LMK05318_LOL_PLL1 = 2,
    LMK05318_LOL_PLL2 = 4,
    LMK05318_LOS_FDET_XO = 8,

    LMK05318_LOPL_DPLL = 16,
    LMK05318_LOFL_DPLL = 32,

};

int lmk05318_check_lock(lmk05318_state_t* d, unsigned* los_msk);

int lmk05318_reg_wr(lmk05318_state_t* d, uint16_t reg, uint8_t out);
int lmk05318_reg_rd(lmk05318_state_t* d, uint16_t reg, uint8_t* val);

#endif
