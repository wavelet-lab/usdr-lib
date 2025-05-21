// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include "ext_simplesync.h"
#include "../ipblks/gpio.h"

#include <stdlib.h>
#include <usdr_logging.h>
#include <string.h>

//
// lmk05318 layout
//
// out0: RF D
// out1: RF A
// out2: RF B
// out3: RF C
//
// out4: CLK B
// out5: CLK C
// out6: CLK D
// out7: CLK A


enum {
    GPIO_SDA     = GPIO0,
    GPIO_SCL     = GPIO1,
    GPIO_1PPS    = GPIO2,
    GPIO_UART_TX = GPIO3,
    GPIO_UART_RX = GPIO4,
    GPIO_ENCLKPWR = GPIO5,
    GPIO_ENGPS    = GPIO6,
    GPIO_SYSREFGEN   = GPIO7,
};

enum {
    I2C_EXTERNAL_CMD_OFF = 16,
};

enum {
    I2C_ADDR_LMK = 0x65,
};

static int simplesync_pd_low_chs(board_ext_simplesync_t* ob)
{
    int res = 0;
    res = res ? res : lmk05318_set_out_mux(&ob->lmk, 0, 0, OUT_OFF);
    res = res ? res : lmk05318_set_out_mux(&ob->lmk, 1, 0, OUT_OFF);
    res = res ? res : lmk05318_set_out_mux(&ob->lmk, 2, 0, OUT_OFF);
    res = res ? res : lmk05318_set_out_mux(&ob->lmk, 3, 0, OUT_OFF);
    res = res ? res : lmk05318_reg_wr_from_map(&ob->lmk, false /*dry_run*/);
    return res;
}

int board_ext_simplesync_init(lldev_t dev,
                              unsigned subdev,
                              unsigned gpio_base,
                              const char* compat,
                              unsigned i2c_loc,
                              board_ext_simplesync_t* ob)
{
    int res = 0;
    unsigned i2ca = MAKE_LSOP_I2C_ADDR(LSOP_I2C_INSTANCE(i2c_loc), LSOP_I2C_BUSNO(i2c_loc), I2C_ADDR_LMK);

    // This breakout is compatible with M.2 key A/E or A+E boards
    if ((strcmp(compat, "m2a+e") != 0) && (strcmp(compat, "m2e") != 0) && (strcmp(compat, "m2a") != 0))
        return -ENODEV;

    // Configure external SDA/SCL
    res = (res) ? res : gpio_config(dev, subdev, gpio_base, GPIO_SDA, GPIO_CFG_ALT0);
    res = (res) ? res : gpio_config(dev, subdev, gpio_base, GPIO_SCL, GPIO_CFG_ALT0);

    res = (res) ? res : gpio_config(dev, subdev, gpio_base, GPIO_SYSREFGEN, GPIO_CFG_ALT0);

//    res = (res) ? res : gpio_config(dev, subdev, gpio_base, GPIO_1PPS, GPIO_CFG_ALT0);
//    res = (res) ? res : gpio_config(dev, subdev, gpio_base, GPIO_1PPS, GPIO_CFG_ALT0);

    res = (res) ? res : gpio_config(dev, subdev, gpio_base, GPIO_ENCLKPWR, GPIO_CFG_OUT);
    res = (res) ? res : gpio_config(dev, subdev, gpio_base, GPIO_ENGPS, GPIO_CFG_OUT);

    res = (res) ? res : gpio_cmd(dev, subdev, gpio_base, GPIO_OUT,
                                 (1 << GPIO_ENCLKPWR) | (1 << GPIO_ENGPS),
                                 (1 << GPIO_ENCLKPWR) | (1 << GPIO_ENGPS));

    if (res)
        return res;

    // Wait for power up
    usleep(50000);

    lmk05318_xo_settings_t xo;
    xo.doubler_enabled = false;
    xo.fdet_bypass = false;
    xo.fref = 26000000;
    xo.pll1_fref_rdiv = 1;
    xo.type = XO_CMOS;

    lmk05318_out_config_t lmk_out[4];

    lmk05318_port_request(&lmk_out[0], 4, 25000000, false, LVCMOS);
    lmk05318_port_request(&lmk_out[1], 5, 25000000, false, LVCMOS);
    lmk05318_port_request(&lmk_out[2], 6, 25000000, false, LVCMOS);
    lmk05318_port_request(&lmk_out[3], 7, 25000000, false, LVCMOS);

    lmk05318_set_port_affinity(&lmk_out[0], AFF_APLL1);
    lmk05318_set_port_affinity(&lmk_out[1], AFF_APLL1);
    lmk05318_set_port_affinity(&lmk_out[2], AFF_APLL1);
    lmk05318_set_port_affinity(&lmk_out[3], AFF_APLL1);

    lmk05318_dpll_settings_t dpll;
    dpll.enabled = false;

    res = lmk05318_create(dev, subdev, i2ca, &xo, &dpll, lmk_out, SIZEOF_ARRAY(lmk_out), &ob->lmk, false /*dry_run*/);
    if(res)
        return res;

    res = simplesync_pd_low_chs(ob); //power down chs 0..3

    res = res ? res : lmk05318_reset_los_flags(&ob->lmk);
    res = res ? res : lmk05318_wait_apll1_lock(&ob->lmk, 10000);
    res = res ? res : lmk05318_sync(&ob->lmk);

    unsigned los_msk;
    lmk05318_check_lock(&ob->lmk, &los_msk, false /*silent*/); //just to log state

    if (res)
        return res;

    USDR_LOG("XDEV", USDR_LOG_ERROR, "SimpleSync lmk05318 has been initialized!\n");
    return 0;
}

#define LO_FREQ_CUTOFF  3500000ul // VCO2_MIN / 7 / 256 = 3069196.43 Hz

int simplesync_tune_lo(board_ext_simplesync_t* ob, uint32_t meas_lo)
{
    int res = 0;

    if(meas_lo < LO_FREQ_CUTOFF)
    {
        res = simplesync_pd_low_chs(ob); //power down chs 0..3
    }
    else
    {
        lmk05318_out_config_t lmk_out[4];

        lmk05318_port_request(&lmk_out[0], 0, meas_lo, false, LVDS);
        lmk05318_port_request(&lmk_out[1], 1, meas_lo, false, LVDS);
        lmk05318_port_request(&lmk_out[2], 2, meas_lo, false, LVDS);
        lmk05318_port_request(&lmk_out[3], 3, meas_lo, false, LVDS);

        lmk05318_set_port_affinity(&lmk_out[0], AFF_APLL2);
        lmk05318_set_port_affinity(&lmk_out[1], AFF_APLL2);
        lmk05318_set_port_affinity(&lmk_out[2], AFF_APLL2);
        lmk05318_set_port_affinity(&lmk_out[3], AFF_APLL2);

        res = lmk05318_solver(&ob->lmk, lmk_out, SIZEOF_ARRAY(lmk_out));
        res = res ? res : lmk05318_reg_wr_from_map(&ob->lmk, false /*dry_run*/);
        if(res)
            return res;

        res = res ? res : lmk05318_softreset(&ob->lmk);
        res = res ? res : lmk05318_reset_los_flags(&ob->lmk);
        res = res ? res : lmk05318_wait_apll2_lock(&ob->lmk, 10000);
        res = res ? res : lmk05318_sync(&ob->lmk);

        unsigned los_msk;
        lmk05318_check_lock(&ob->lmk, &los_msk, false /*silent*/); //just to log state
    }

    return res;
}
