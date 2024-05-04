// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef LMK04832_H
#define LMK04832_H

#include <usdr_lowlevel.h>

// Programming order
// 1. lmk04832_reset_and_test
// 2. lmk04832_configure_layout
// 3..n lmk04832_configure_dports

int lmk04832_reset_and_test(lldev_t dev, subdev_t subdev, lsopaddr_t addr);
int lmk04832_power_down(lldev_t dev, subdev_t subdev, lsopaddr_t addr);

enum lmk04832_dualport_flags {
    LMK04832_DP_ENALED = 1,
    LMK04832_DP_OHIGHCURR = 2,
    LMK04832_DP_IHIGHCURR = 4,
    LMK04832_DP_CLK_X_SYSREF_EN = 8,
    LMK04832_DP_CLK_INVERT = 16,
    LMK04832_DP_PHASE_HALF_CORR = 32,
    LMK04832_DP_CLK_Y_SYSREF_EN = 64,
    LMK04832_DP_SYSREF_INVERT = 128,
    LMK04832_DP_SYSREF_HALF_CORR = 256,
};

enum lmk04832_port_fmt {
    LMK04832_DP_POWERDOWN = 0,
    LMK04832_DP_LVDS = 1,
    LMK04832_DP_HSDS_6MA = 2,
    LMK04832_DP_HSDS_8MS = 3,
    LMK04832_DP_LVPECL_1600MV = 4,
    LMK04832_DP_LVPECL_2000MV = 5,
    LMK04832_DP_LVPECL = 6,
    LMK04832_DP_CML_16MA = 7,
    LMK04832_DP_CML_24MA = 8,
    LMK04832_DP_CML_32MA = 9,
    LMK04832_DP_CMOS_OFF_INV = 10,
    LMK04832_DP_CMOS_NORM_OFF = 11,
    LMK04832_DP_CMOS_INV_INV = 12,
    LMK04832_DP_CMOS_INV_NORM = 13,
    LMK04832_DP_CMOS_NORM_INV = 14,
    LMK04832_DP_CMOS_NORM_NORM = 15,
};

struct lmk04832_dualport_config {
    unsigned divider;
    unsigned div_delay;
    unsigned sysref_div_delay;
    uint8_t flags;
    uint8_t portx_fmt;
    uint8_t porty_fmt;
};
typedef struct lmk04832_dualport_config lmk04832_dualport_config_t;

enum lmk04832_dport_num {
    LMK04832_PORT_CLK0_1 = 0,
    LMK04832_PORT_CLK2_3 = 1,
    LMK04832_PORT_CLK4_5 = 2,
    LMK04832_PORT_CLK6_7 = 3,
    LMK04832_PORT_CLK8_9 = 4,
    LMK04832_PORT_CLK10_11 = 5,
    LMK04832_PORT_CLK12_13 = 6,
};

int lmk04832_configure_dports(lldev_t dev, subdev_t subdev, lsopaddr_t addr,
                              unsigned portx, const lmk04832_dualport_config_t* pcfg);

enum lmk04832_clk_route {
    LMK04832_CLKPATH_CLK1IN,
    LMK04832_CLKPATH_OSCIN_PLL2,

    LMK04832_CLKPATH_CLK0IN_PLL2,
    LMK04832_CLKPATH_CLK1IN_PLL2,
    LMK04832_CLKPATH_CLK2IN_PLL2,

    LMK04832_CLKPATH_CLK0IN_PLL1_OSCIN_PLL2,
    LMK04832_CLKPATH_CLK1IN_PLL1_OSCIN_PLL2,
    LMK04832_CLKPATH_CLK2IN_PLL1_OSCIN_PLL2,
};


struct lmk04832_layout {
    unsigned clk_route;

    unsigned in_frequency;
    unsigned ext_vco_frequency; // Valid when PLL1 is active
    unsigned distribution_frequency;
};

struct lmk04832_layout_out {
    unsigned vco_frequency;
    unsigned in_frequency;
};

typedef struct lmk04832_layout lmk04832_layout_t;
typedef struct lmk04832_layout_out lmk04832_layout_out_t;

int lmk04832_sysref_div_set(lldev_t dev, subdev_t subdev, lsopaddr_t addr,
                            unsigned divider);

int lmk04832_configure_layout(lldev_t dev, subdev_t subdev, lsopaddr_t addr,
                              const lmk04832_layout_t* pcfg, lmk04832_layout_out_t* outp);


int lmk04832_check_lock(lldev_t dev, subdev_t subdev, lsopaddr_t addr,
                        bool* locked);


#endif
