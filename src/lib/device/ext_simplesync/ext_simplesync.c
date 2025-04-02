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

enum {
    FREQ_DELTA_HZ = 2,
};

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

#if 0
    res = lmk05318_create(dev, subdev, i2ca, 0, &ob->lmk);
#else
    lmk05318_xo_settings_t xo;
    xo.doubler_enabled = true;
    xo.fdet_bypass = true;
    xo.fref = 26000000;
    xo.pll1_fref_rdiv = 1;
    xo.type = XO_CMOS;

    lmk05318_out_config_t cfg[4];
    lmk05318_port_request(cfg, 4, 25000000, FREQ_DELTA_HZ, FREQ_DELTA_HZ, false, LVCMOS);
    lmk05318_port_request(cfg, 5, 25000000, FREQ_DELTA_HZ, FREQ_DELTA_HZ, false, LVCMOS);
    lmk05318_port_request(cfg, 6, 25000000, FREQ_DELTA_HZ, FREQ_DELTA_HZ, false, LVCMOS);
    lmk05318_port_request(cfg, 7, 25000000, FREQ_DELTA_HZ, FREQ_DELTA_HZ, false, LVCMOS);
    lmk05318_set_port_affinity(cfg, 4, AFF_APLL1);
    lmk05318_set_port_affinity(cfg, 5, AFF_APLL1);
    lmk05318_set_port_affinity(cfg, 6, AFF_APLL1);
    lmk05318_set_port_affinity(cfg, 7, AFF_APLL1);

    res = lmk05318_create_ex(dev, subdev, i2ca, &xo, false, cfg, 4, &ob->lmk, false /*dry_run*/);
#endif

    if (res)
        return res;

    USDR_LOG("XDEV", USDR_LOG_ERROR, "SimpleSync lmk05318 has been initialized!\n");
    return 0;
}



int simplesync_tune_lo(board_ext_simplesync_t* ob, uint32_t meas_lo)
{

#if 0
    unsigned div = 255;
    int res = lmk05318_tune_apll2(&ob->lmk, meas_lo, &div);

    for (unsigned p = 0; p < 4; p++) {
        res = (res) ? res : lmk05318_set_out_div(&ob->lmk, p, div);
        res = (res) ? res : lmk05318_set_out_mux(&ob->lmk, p, false, meas_lo < 1e6 ? OUT_OFF : LVDS);
    }
#else
    lmk05318_out_config_t cfg[4];
    lmk05318_port_request(cfg, 0, meas_lo, FREQ_DELTA_HZ, FREQ_DELTA_HZ, false, meas_lo < 1e6 ? OUT_OFF : LVDS);
    lmk05318_port_request(cfg, 1, meas_lo, FREQ_DELTA_HZ, FREQ_DELTA_HZ, false, meas_lo < 1e6 ? OUT_OFF : LVDS);
    lmk05318_port_request(cfg, 2, meas_lo, FREQ_DELTA_HZ, FREQ_DELTA_HZ, false, meas_lo < 1e6 ? OUT_OFF : LVDS);
    lmk05318_port_request(cfg, 3, meas_lo, FREQ_DELTA_HZ, FREQ_DELTA_HZ, false, meas_lo < 1e6 ? OUT_OFF : LVDS);
    lmk05318_set_port_affinity(cfg, 0, AFF_APLL2);
    lmk05318_set_port_affinity(cfg, 1, AFF_APLL2);
    lmk05318_set_port_affinity(cfg, 2, AFF_APLL2);
    lmk05318_set_port_affinity(cfg, 3, AFF_APLL2);

    int res = lmk05318_solver(&ob->lmk, cfg, 4, false);
#endif
    return res;
}
