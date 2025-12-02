// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef LMS8001_H
#define LMS8001_H

// LMS8001 control logic mostly for block specific perfective
#include <stdint.h>
#include <usdr_lowlevel.h>

#define LMS8_BIT(x)  (1ull << (x))

enum lms8001_tune_flags {
    LMS8001_INT_MODE = LMS8_BIT(0),
    LMS8001_IQ_GEN = LMS8_BIT(1),
    LMS8001_PDIV2 = LMS8_BIT(2),

    LMS8001_SELF_BIAS_XBUF = LMS8_BIT(16),
};


struct lms8001_pll_configuration {
    uint16_t PLL_VREG;
    uint16_t PLL_CFG_XBUF;
    //uint16_t PLL_CAL_AUTO0;
    uint16_t PLL_CAL_AUTO1;
    uint16_t PLL_CAL_AUTO2;
    uint16_t PLL_CAL_AUTO3;
    //uint16_t PLL_CAL_MAN;
    // uint16_t PLL_CFG_SEL0;
    // uint16_t PLL_CFG_SEL1;
    // uint16_t PLL_CFG_SEL2;
    uint16_t PLL_CFG;
    //uint16_t PLL_LODIST_CFG1;
    //uint16_t PLL_LODIST_CFG2;
};
typedef struct lms8001_pll_configuration lms8001_pll_configuration_t;

struct lms8001_pll_state {
    uint16_t ENABLE;
    uint16_t LPF_CFG1;
    uint16_t LPF_CFG2;
    uint16_t CP_CFG0;
    uint16_t CP_CFG1;
    uint16_t VCO_FREQ;
    uint16_t VCO_CFG;
    uint16_t FF_CFG;
    uint16_t SDM_CFG;
    uint16_t FRACMODL;
    uint16_t FRACMODH;
    uint16_t LODIST_CFG;
    uint16_t FLOCK_CFG1;
    uint16_t FLOCK_CFG2;
    uint16_t FLOCK_CFG3;
};
typedef struct lms8001_pll_state lms8001_pll_state_t;

enum {
    LMS8001_CHANNELS = 4,
    LMS8001_PROFILES = 8,
};

enum lms8_stepping {
    LMS8_MPW2015 = 0,
    LMS8_MPW2024 = 1,
};

struct lms8001_state {
    lldev_t dev;
    unsigned subdev;
    unsigned lsaddr;

    // Chip stepping
    unsigned stepping;

    // Enabled channel masks
    unsigned chan_mask;

    // Active profile during PLL operations
    unsigned act_profile;

    // Cached state for fast access
    lms8001_pll_configuration_t pll;
    lms8001_pll_state_t pll_profiles[LMS8001_PROFILES];
};
typedef struct lms8001_state lms8001_state_t;


int lms8001_create(lldev_t dev, unsigned subdev, unsigned lsaddr, unsigned stepping, lms8001_state_t *out);
int lms8001_destroy(lms8001_state_t* m);

int lms8001_core_enable(lms8001_state_t* state, bool enable);

int lms8001_tune(lms8001_state_t* state, unsigned fref, uint64_t out);
int lms8001_ch_enable(lms8001_state_t* state, unsigned mask);

int lms8001a_ch_enable(lms8001_state_t* state, unsigned mask, unsigned lna_loss[4], unsigned pa_loss[4]);

// lna_loss == ~0 means bypass LNA
int lms8001a_ch_lna_pa_set(lms8001_state_t* state, unsigned chan, unsigned lna_loss, unsigned pa_loss);
int lms8001b_hlmix_loss_set(lms8001_state_t* state, unsigned chan, unsigned loss);

int lms8001_reg_set(lms8001_state_t* m, uint16_t addr, uint16_t val);
int lms8001_reg_get(lms8001_state_t* m, uint16_t addr, uint16_t* oval);


int lms8001_smart_tune(lms8001_state_t* m, unsigned tune_flags, uint64_t flo, int fref, int loopbw, float phasemargin, float bwef, int flock_N);


int lms8001_temp_start(lms8001_state_t* m);
int lms8001_temp_get(lms8001_state_t* m, int *temp256);


#endif
