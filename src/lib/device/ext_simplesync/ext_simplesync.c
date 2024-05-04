// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include "ext_simplesync.h"
#include "../ipblks/gpio.h"

#include <stdlib.h>
#include <usdr_logging.h>

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

#if 0   //unused
static int dev_gpi_get32(lldev_t dev, unsigned bank, unsigned* data)
{
    return lowlevel_reg_rd32(dev, 0, 16 + (bank / 4), data);
}
#endif

int board_ext_simplesync_init(lldev_t dev,
                             unsigned subdev,
                             unsigned gpio_base,
                             ext_i2c_func_t func,
                             board_ext_simplesync_t* ob)
{
    int res = 0;

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

    res = lmk05318_create(dev, subdev, I2C_ADDR_LMK << I2C_EXTERNAL_CMD_OFF, func, &ob->lmk);
    if (res)
        return res;

    USDR_LOG("XDEV", USDR_LOG_ERROR, "SimpleSync lmk05318 has been initialized!\n");
    return 0;
}



int simplesync_tune_lo(board_ext_simplesync_t* ob, uint32_t meas_lo)
{
    unsigned div = 255;
    int res = lmk05318_tune_apll2(&ob->lmk, meas_lo, &div);

    for (unsigned p = 0; p < 4; p++) {
        res = (res) ? res : lmk05318_set_out_div(&ob->lmk, p, div);
        res = (res) ? res : lmk05318_set_out_mux(&ob->lmk, p, false, meas_lo < 1e6 ? OUT_OFF : LVDS);
    }

    return res;
}
