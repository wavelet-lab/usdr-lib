// Copyright (c) 2025 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef LMX1204_H
#define LMX1204_H

#include <usdr_lowlevel.h>

#define LMX1204_OUT_CNT 4

enum
{
    LMX1204_CH0 = 0,
    LMX1204_CH1 = 1,
    LMX1204_CH2 = 2,
    LMX1204_CH3 = 3,
    LMX1204_CH_LOGIC = 4,
};

struct lmx1204_sysrefout_channel_delay
{
    uint8_t phase;
    uint8_t q;
    uint8_t i;
};
typedef struct lmx1204_sysrefout_channel_delay lmx1204_sysrefout_channel_delay_t;

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

    bool ch_en[LMX1204_OUT_CNT + 1];        // 4 + logic ch
    bool clkout_en[LMX1204_OUT_CNT + 1];
    bool sysrefout_en[LMX1204_OUT_CNT + 1];

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
    bool logiclk_div_bypass;
    uint16_t logiclk_div;

    uint8_t sysref_delay_div;
    uint8_t sysref_div_pre;
    uint16_t sysref_div;
    uint8_t sysref_delay_scale;
    lmx1204_sysrefout_channel_delay_t sysref_indiv_ch_delay[LMX1204_OUT_CNT + 1]; // 4 + logic ch
};
typedef struct lmx1204_state lmx1204_state_t;

static inline void lmx1204_init_sysrefout_ch_delay(lmx1204_state_t* st, unsigned ch, uint8_t phase, uint8_t i, uint8_t q)
{
    if(ch > LMX1204_CH_LOGIC)
        return;

    st->sysref_indiv_ch_delay[ch].phase = phase;
    st->sysref_indiv_ch_delay[ch].i = i;
    st->sysref_indiv_ch_delay[ch].q = q;
}

struct lmx1204_stats
{
    float temperature;
    uint8_t vco_sel;
    uint8_t lock_detect_status;
};
typedef struct lmx1204_stats lmx1204_stats_t;

enum
{
    LMX1204_FMT_LVDS = 0,
    LMX1204_FMT_LVPECL = 1,
    LMX1204_FMT_CML = 2,
};

enum
{
    LMX1204_CONTINUOUS = 0,
    LMX1204_PULSER = 1,
    LMX1204_REPEATER = 2,
};

int lmx1204_read_status(lmx1204_state_t* st, lmx1204_stats_t* status);
int lmx1204_calibrate(lmx1204_state_t* st);
int lmx1204_wait_pll_lock(lmx1204_state_t* st, unsigned timeout);
int lmx1204_reset_main_divider(lmx1204_state_t* st, bool set_flag);
int lmx1204_solver(lmx1204_state_t* st, bool prec_mode, bool dry_run);
int lmx1204_get_temperature(lmx1204_state_t* st, float* value);
int lmx1204_reload_sysrefout_ch_delay(lmx1204_state_t* st);
int lmx1204_create(lldev_t dev, unsigned subdev, unsigned lsaddr, lmx1204_state_t* st);
int lmx1204_destroy(lmx1204_state_t* st);

int lmx1204_sysref_windowing_beforesync(lmx1204_state_t* st);
int lmx1204_sysref_windowing_capture(lmx1204_state_t* st);
int lmx1204_sysref_windowing_aftersync(lmx1204_state_t* st);

int lmx1204_loaddump(lmx1204_state_t* st);

#endif // LMX1204_H
