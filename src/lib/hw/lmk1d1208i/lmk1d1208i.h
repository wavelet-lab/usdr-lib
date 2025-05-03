// Copyright (c) 2025 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef LMK1D1208I_H
#define LMK1D1208I_H

#include <usdr_lowlevel.h>

#define LMK1D1208I_IN_CNT 2
#define LMK1D1208I_BANK_CNT 2
#define LMK1D1208I_OUT_CNT 8

struct lmk1d1208i_state {
    lldev_t dev;
    unsigned subdev;
    unsigned lsaddr;
};
typedef struct lmk1d1208i_state lmk1d1208i_state_t;

enum lmk1d1208i_bank_sel
{
    LMK1D1208I_IN0 = 1,
    LMK1D1208I_IN1 = 0,
};
typedef enum lmk1d1208i_bank_sel lmk1d1208i_bank_sel_t;

enum lmk1d1208i_amp_sel
{
    LMK1D1208I_STANDARD_LVDS = 0,
    LMK1D1208I_BOOSTED_LVDS = 1,
};
typedef enum lmk1d1208i_amp_sel lmk1d1208i_amp_sel_t;

struct lmk1d1208i_config
{
    struct
    {
        bool enabled;
    }
    in[LMK1D1208I_IN_CNT];

    struct
    {
        lmk1d1208i_bank_sel_t sel;
        bool mute;
    }
    bank[LMK1D1208I_BANK_CNT];

    struct
    {
        bool enabled;
        lmk1d1208i_amp_sel_t amp;
    }
    out[LMK1D1208I_OUT_CNT];
};
typedef struct lmk1d1208i_config lmk1d1208i_config_t;


int lmk1d1208i_create(lldev_t dev, unsigned subdev, unsigned lsaddr, const lmk1d1208i_config_t* cfg, lmk1d1208i_state_t* st);
int lmk1d1208i_destroy(lmk1d1208i_state_t* st);

#endif // LMK1D1208I_H
