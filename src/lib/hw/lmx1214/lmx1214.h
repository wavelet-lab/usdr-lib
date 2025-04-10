// Copyright (c) 2025 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef LMX1214_H
#define LMX1214_H

#include <usdr_lowlevel.h>

#define LMX1214_OUT_CNT 4

enum
{
    LMX1214_FMT_LVDS = 0,
    LMX1214_FMT_CML = 2,
};

struct lmx1214_auxclkout_cfg
{
    uint64_t freq;
    bool enable;
    uint8_t fmt;
};
typedef struct lmx1214_auxclkout_cfg lmx1214_auxclkout_cfg_t;

struct lmx1214_state
{
    lldev_t dev;
    unsigned subdev;
    unsigned lsaddr;

    uint64_t clkin;

    uint8_t clk_mux;
    uint8_t clk_div;

    uint8_t auxclk_div_pre;
    uint16_t auxclk_div;
    bool auxclk_div_byp;

    uint64_t clkout;
    bool clkout_enabled[LMX1214_OUT_CNT];
    lmx1214_auxclkout_cfg_t auxclkout;
};
typedef struct lmx1214_state lmx1214_state_t;


int lmx1214_create(lldev_t dev, unsigned subdev, unsigned lsaddr, lmx1214_state_t* st);
int lmx1214_destroy(lmx1214_state_t* st);
int lmx1214_solver(lmx1214_state_t* st, uint64_t in, uint64_t out, bool* out_en, lmx1214_auxclkout_cfg_t* aux, bool dry_run);
int lmx1214_get_temperature(lmx1214_state_t* st, float* value);
int lmx1214_sync_clr(lmx1214_state_t* st);

#endif // LMX1214_H
