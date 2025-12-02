// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef EXT_FE_CH4_400_7200_H
#define EXT_FE_CH4_400_7200_H

#include <usdr_port.h>
#include <usdr_lowlevel.h>
#include <stdbool.h>
#include "../device.h"
#include "../device_vfs.h"


#define H_CHA 0
#define H_CHB 1
#define H_CHC 2
#define H_CHD 3

#define FE_MAX_HW_CHANS 4

#define FE_CTRL_REGS 10

enum rx_filterbank {
    RX_FB_400_1000M,
    RX_FB_1000_2000M,
    RX_FB_2000_3500M,
    RX_FB_2500_5000M,
    RX_FB_3500_7100M,

    RX_FB_AUTO = 8,
};

enum antenna_cfg {
    ANT_RX_TRX,   // RX connected to RX antenna and TX connected to TRX antenna
    ANT_TRX_TERM, // RX connected to TRX antenna and TX terminated
    ANT_RX_TERM,  // RX connected to RX antenna and TX terminated
    ANT_LOOPBACK, // RX connected to TX port through attenuator

    ANT_HW_TDD,   // TRX antenna is dynamically switched to TX/RX ports based on burst information
    ANT_OFF,
};

struct fe_chan_config {
    uint8_t rx_fb_sel; // rx_filterbank
    uint8_t rx_dsa;
    uint8_t ant_sel;   // antenna selector
    uint8_t tx_ss; // Single stage PA

    uint8_t tx_en; // Channel enabled on device side
    uint8_t rx_en; // Channel enabled on device side

    // For auto band & filter selection
    uint64_t rx_freq;
};
typedef struct fe_chan_config fe_chan_config_t;

struct ext_fe_ch4_400_7200 {
    lldev_t dev;
    unsigned subdev;

    unsigned gpio_base;
    unsigned hsgpio_base; // HighSpeed gpio over LVDS


    uint32_t fe_exp_regs[FE_CTRL_REGS];
    uint32_t debug_fe_reg_last;
    uint32_t debug_fe_usr_last;

    // High level control
    uint8_t ref_gps; // Globally enable GPS
    uint8_t if_vbyp; // Globally enable IF BYP
    fe_chan_config_t ucfg[FE_MAX_HW_CHANS];
};

typedef struct ext_fe_ch4_400_7200 ext_fe_ch4_400_7200_t;

int ext_fe_ch4_400_7200_init(lldev_t dev,
                             unsigned subdev,
                             unsigned gpio_base,
                             const char *params,
                             const char *compat,
                             ext_fe_ch4_400_7200_t* ob);
int ext_fe_destroy(ext_fe_ch4_400_7200_t* dfe);

int ext_fe_rx_freq_set(ext_fe_ch4_400_7200_t* def, unsigned chno, uint64_t freq);
int ext_fe_rx_chan_en(ext_fe_ch4_400_7200_t* def, unsigned ch_fe_mask_rx);
int ext_fe_tx_chan_en(ext_fe_ch4_400_7200_t* def, unsigned ch_fe_mask_tx);

int ext_fe_rx_gain_set(ext_fe_ch4_400_7200_t* def, unsigned chno, unsigned gain, unsigned* actual_gain);

int ext_fe_set_dac(ext_fe_ch4_400_7200_t* brd, unsigned value);

#endif
