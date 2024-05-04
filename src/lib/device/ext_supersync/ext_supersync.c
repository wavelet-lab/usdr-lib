// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include "ext_supersync.h"
#include "../ipblks/gpio.h"

#include <stdlib.h>
#include <usdr_logging.h>

enum {
    GPIO_SDA     = GPIO0,
    GPIO_SCL     = GPIO1,
    GPIO_1PPS    = GPIO2,
    GPIO_UART_TX = GPIO3,
    GPIO_UART_RX = GPIO4,
};

enum {
    I2C_EXTERNAL_CMD_OFF = 16,
};

enum {
    I2C_ADDR_LMK = 0x64,
};

#if 0   //unused
static int dev_gpi_get32(lldev_t dev, unsigned bank, unsigned* data)
{
    return lowlevel_reg_rd32(dev, 0, 16 + (bank / 4), data);
}
#endif

int board_ext_supersync_init(lldev_t dev,
                             unsigned subdev,
                             unsigned gpio_base,
                             ext_i2c_func_t func,
                             board_ext_supersync_t* ob)
{
    int res = 0;
    // uint8_t dummy[4];

    // Configure external SDA/SCL
    res = (res) ? res : gpio_config(dev, subdev, gpio_base, GPIO_SDA, GPIO_CFG_IN);
    res = (res) ? res : gpio_config(dev, subdev, gpio_base, GPIO_SCL, GPIO_CFG_IN);
    res = (res) ? res : gpio_config(dev, subdev, gpio_base, GPIO_1PPS, GPIO_CFG_ALT0);
    if (res)
        return res;


    // Wait for power up
    usleep(50000);

    res = lmk5c33216_create(dev, subdev, I2C_ADDR_LMK << I2C_EXTERNAL_CMD_OFF, func, &ob->lmk);
    if (res)
        return res;


    USDR_LOG("XDEV", USDR_LOG_ERROR, "SuperSync 5c33216 has been initialized!\n");

    return 0;
}
