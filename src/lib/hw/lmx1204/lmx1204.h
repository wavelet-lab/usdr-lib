// Copyright (c) 2025 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef LMX1204_H
#define LMX1204_H

#include <usdr_lowlevel.h>

#define LMX1204_OUT_CNT 4

struct lmx1204_state
{
    lldev_t  dev;
    unsigned subdev;
    unsigned lsaddr;

    uint64_t clkin;
    uint64_t sysrefreq;
    bool     filter_mode; //affects when clkin==clkout
    uint8_t  sysref_mode; //enum sysref_mode_options

    bool sysref_en;

    bool ch_en[LMX1204_OUT_CNT];
    bool clkout_en[LMX1204_OUT_CNT];
    bool sysrefout_en[LMX1204_OUT_CNT];

    bool logic_en;
    bool logiclkout_en;
    bool logisysrefout_en;

    double   clkout;
    double   sysrefout;
    double   logiclkout;

    uint8_t  logiclkout_fmt;
    uint8_t  logisysrefout_fmt;

    //

    uint8_t clk_mux; //enum clk_mux_options
    uint8_t clk_mult_div;

    uint8_t smclk_div_pre;
    uint8_t smclk_div;

    uint8_t logiclk_div_pre;
    uint16_t logiclk_div;

    uint8_t sysref_delay_div;
    uint8_t sysref_div_pre;
    uint16_t sysref_div;
};
typedef struct lmx1204_state lmx1204_state_t;

enum
{
    LMX1204_FMT_LVDS = 0,
    LMX1204_FMT_LVPECL = 1,
    LMX1204_FMT_CML = 2,
};


int lmx1204_get_temperature(lmx1204_state_t* st, float* value);
int lmx1204_create(lldev_t dev, unsigned subdev, unsigned lsaddr, lmx1204_state_t* st);
int lmx1204_destroy(lmx1204_state_t* st);


#endif // LMX1204_H
