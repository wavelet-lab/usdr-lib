// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef LMK05318_H
#define LMK05318_H

#include <usdr_lowlevel.h>
#include "lmk05318_solver.h"

#define MAX_OUT_PORTS 8
#define MAX_REAL_PORTS (MAX_OUT_PORTS - 2)

struct lmk05318_state {
    lldev_t dev;
    unsigned subdev;
    unsigned lsaddr;

    // Ref deviders
    unsigned fref_pll2_div_rp; // 3 to 6
    unsigned fref_pll2_div_rs; // 1 - 32

    // VCO2 freq
    uint64_t vco2_freq;

    struct {
        uint32_t fref;
        int type;
        bool doubler_enabled;
        bool fdet_bypass;
    } xo;
};

enum lmk05318_type {
    LVDS,
    CML,
    LVPECL,
    LVCMOS,
    OUT_OFF,
};

typedef struct lmk05318_state lmk05318_state_t;
typedef enum lmk05318_type lmk05318_type_t;

struct lmk05318_out_config
{
    unsigned port; //0..7
    bool solved;

    struct
    {
        uint32_t freq;
        unsigned freq_delta_plus, freq_delta_minus;
        bool revert_phase;
        lmk05318_type_t type;
    } wanted;

    struct
    {
        double freq;
        uint64_t out_div;
        int mux;
    } result;

    //
    uint64_t max_odiv;
    uint32_t freq_min, freq_max;
    uint32_t pd_min, pd_max;
    //
};
typedef struct lmk05318_out_config lmk05318_out_config_t;

int lmk05318_create(lldev_t dev, unsigned subdev, unsigned lsaddr, lmk05318_state_t* out);

int lmk05318_tune_apll2(lmk05318_state_t* d, uint32_t freq, unsigned *last_div);
int lmk05318_set_out_div(lmk05318_state_t* d, unsigned port, uint64_t div);
int lmk05318_set_out_mux(lmk05318_state_t* d, unsigned port, bool pll1, unsigned otype);



int lmk05318_reg_wr(lmk05318_state_t* d, uint16_t reg, uint8_t out);
int lmk05318_reg_rd(lmk05318_state_t* d, uint16_t reg, uint8_t* val);

int lmk05318_set_xo_fref(lmk05318_state_t* d, uint32_t xo_fref, int xo_type,
                         bool xo_doubler_enabled, bool xo_fdet_bypass);

int lmk05318_tune_apll1(lmk05318_state_t* d,
                        uint32_t xo_fref, int xo_type,
                        bool xo_doubler_enabled, bool xo_fdet_bypass, bool dpll_mode);

int lmk05318_solver(lmk05318_state_t* d, lmk05318_out_config_t* _outs, unsigned n_outs);

#endif
