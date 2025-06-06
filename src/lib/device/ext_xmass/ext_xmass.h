// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef EXT_XMASS_H
#define EXT_XMASS_H

#include <usdr_port.h>
#include <usdr_lowlevel.h>
#include <stdbool.h>
#include "../device.h"
#include "../device_vfs.h"

#include "../hw/lmk05318/lmk05318.h"

struct board_xmass {
    lmk05318_state_t lmk;

    // Board configuration
    bool distrib_ext;
    bool distrib_local;
    bool pwr_gps_en;
    bool pwr_pa_en;
    bool pwr_noise_en;

    bool cal_dst_lna3;     // 0 -- CAL_TO_RX;     1 -- CAL_TO_LNA3
    bool cal_src_noise;    // 0 -- CAL_CW;        1 -- CAL_NOISE
    bool pps_from_sdra;    // 0 -- PPS_LMK;       1 -- PPS_SDRA
    bool loopback_en;      // 0 -- Normal;        1 -- Loopback (TX & Cal)
    bool loopback_mode_tx; // 0 -- RF_CAL_SOURCE; 1 -- TX_SOURCE

    unsigned gpio_base;
    unsigned i2c_xraa;

    unsigned refclk;
    unsigned calfreq;
};
typedef struct board_xmass board_xmass_t;

int board_xmass_init(lldev_t dev,
                     unsigned subdev,
                     unsigned gpio_base,
                     const char* compat,
                     unsigned int i2c_loc,
                     board_xmass_t* ob);


// High level contorl interface
int board_xmass_ctrl_cmd_wr(board_xmass_t* ob, uint32_t addr, uint32_t reg);
int board_xmass_ctrl_cmd_rd(board_xmass_t* ob, uint32_t addr, uint32_t* preg);

int board_xmass_tune_cal_lo(board_xmass_t* ob, uint32_t callo);

// 0 - off
// 1 - LO
// 2 - noise
// 3 - LO    - LNA3
// 4 - noise - LNA3
int board_xmass_sync_source(board_xmass_t* ob, unsigned sync_src);


#endif
