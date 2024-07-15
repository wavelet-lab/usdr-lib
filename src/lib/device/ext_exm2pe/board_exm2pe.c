// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include <usdr_logging.h>
#include <string.h>

#include "board_exm2pe.h"
#include "../ipblks/gpio.h"
#include "../ipblks/uart.h"

#include "../hw/dac80501/dac80501.h"
#include "../common/parse_params.h"


enum {
    GPIO_SDA     = GPIO0,
    GPIO_SCL     = GPIO1,
    GPIO_1PPS    = GPIO2,
    GPIO_UART_TX = GPIO3,
    GPIO_UART_RX = GPIO4,
    GPIO_EN_OSC  = GPIO5,
    GPIO_EN_GPS  = GPIO6,
};

enum {
    I2C_ADDR_DAC = 0x48,
    I2C_EXTERNAL_CMD_OFF = 16,
};

int board_exm2pe_init(lldev_t dev,
                      unsigned subdev,
                      unsigned gpio_base,
                      const char* params,
                      const char* compat,
                      unsigned i2c_loc,
                      board_exm2pe_t* ob)
{
    int res = 0;
    uint8_t gps_en = 0;
    uint8_t osc_en = 0;
    long dac_val = 0;
    unsigned i2ca_dac = MAKE_LSOP_I2C_ADDR(LSOP_I2C_INSTANCE(i2c_loc), LSOP_I2C_BUSNO(i2c_loc), I2C_ADDR_DAC);

    // This breakout is compatible wi M.2 key A/E or A+E boards
    if ((strcmp(compat, "m2a+e") != 0) && (strcmp(compat, "m2e") != 0) && (strcmp(compat, "m2a") != 0))
        return -ENODEV;

    ob->dev = dev;
    ob->subdev = subdev;
    ob->gpio_base = gpio_base;
    ob->i2c_loc = i2c_loc;
    ob->dac_present = true;

    // Configure  gpio dir
    res = (res) ? res : gpio_config(dev, subdev, gpio_base, GPIO_SDA, GPIO_CFG_ALT0);
    res = (res) ? res : gpio_config(dev, subdev, gpio_base, GPIO_SCL, GPIO_CFG_ALT0);

    // Check DAC
    res = (res) ? res : gpio_config(dev, subdev, gpio_base, GPIO_EN_OSC, GPIO_CFG_OUT);
    res = (res) ? res : gpio_config(dev, subdev, gpio_base, GPIO_EN_GPS, GPIO_CFG_OUT);
    res = (res) ? res : gpio_cmd(dev, subdev, gpio_base, GPIO_OUT,
                                 (1 << GPIO_EN_OSC) | (1 << GPIO_EN_GPS),
                                 (1 << GPIO_EN_OSC) | (1 << GPIO_EN_GPS));
    usleep(10000);

    res = (res) ? res : dac80501_init(dev, subdev, i2ca_dac, DAC80501_CFG_REF_DIV_GAIN_MUL);
    if (res) {
        USDR_LOG("M2PE", USDR_LOG_WARNING, "External DAC not recognized error=%d\n", res);
        //return -ENODEV;
        ob->dac_present = false;
    }

    enum { P_GPS, P_OSC, P_DAC, P_UART };
    static const char* ppars[] = {
        "gps_",
        "osc_",
        "dac_",
        "uart_",
        NULL,
    };
    struct param_data pd[SIZEOF_ARRAY(ppars)];
    memset(pd, 0, sizeof(pd));
    if (params != 0) {
        const char* fault = NULL;
        parse_params(params, ':', ppars, pd, &fault);
        if (fault) {
            USDR_LOG("M2PE", USDR_LOG_ERROR, "Unrecognized front end option: `%s`\n", fault);
            return -EINVAL;
        }

        if (((res = is_param_on(&pd[P_GPS]))) >= 0) {
            gps_en = res;
        }
        if (((res = is_param_on(&pd[P_OSC]))) >= 0) {
            osc_en = res;
        }
        if (get_param_long(&pd[P_DAC], &dac_val) == 0) {
            if ((dac_val < 0) || (dac_val > 65535)) {
                USDR_LOG("M2PE", USDR_LOG_ERROR, "DAC value must be in range [0;65535]\n");
                return -EINVAL;
            }

            res = board_exm2pe_set_dac(ob, dac_val);
        }
    }
    res = 0;

    res = (res) ? res : gpio_config(dev, subdev, gpio_base, GPIO_1PPS, GPIO_CFG_ALT0);
    res = (res) ? res : gpio_config(dev, subdev, gpio_base, GPIO_UART_TX, GPIO_CFG_ALT0);
    res = (res) ? res : gpio_config(dev, subdev, gpio_base, GPIO_UART_RX, GPIO_CFG_ALT0);
//    res = (res) ? res : gpio_config(dev, subdev, gpio_base, GPIO_EN_OSC, GPIO_CFG_OUT);
//    res = (res) ? res : gpio_config(dev, subdev, gpio_base, GPIO_EN_GPS, GPIO_CFG_OUT);

    res = (res) ? res : gpio_cmd(dev, subdev, gpio_base, GPIO_OUT,
                                 (1 << GPIO_EN_OSC) | (1 << GPIO_EN_GPS),
                                 (osc_en << GPIO_EN_OSC) | (gps_en << GPIO_EN_GPS));

    if (is_param_on(&pd[P_UART]) == 1) {
        // check uart
        char b[4096];
        uart_core_t uc;
        res = (res) ? res : uart_core_init(dev, subdev, DEFAULT_UART_IO, &uc);
        res = (res) ? res : uart_core_rx_collect(&uc, sizeof(b), b, 2250);
        USDR_LOG("M2PE", USDR_LOG_ERROR, "UART: len=%d: `%s`\n", res, b);
        if (res > 0)
            res = 0;
    }
    if (res)
        return res;

    USDR_LOG("M2PE", USDR_LOG_INFO, "EXM2PE initialized %s\n",
             ob->dac_present ? "DAC is ok" : "no DAC");
    return 0;
}

int board_exm2pe_enable_gps(board_exm2pe_t* brd, bool en)
{
    USDR_LOG("M2PE", USDR_LOG_ERROR, "GPS power: %d\n", en);
    return gpio_cmd(brd->dev, brd->subdev, brd->gpio_base,
                    GPIO_OUT, (1 << GPIO_EN_GPS), ((en ? 1 : 0) << GPIO_EN_GPS));
}

int board_exm2pe_enable_osc(board_exm2pe_t* brd, bool en)
{
    USDR_LOG("M2PE", USDR_LOG_ERROR, "OSC power: %d\n", en);
    return gpio_cmd(brd->dev, brd->subdev, brd->gpio_base,
                    GPIO_OUT, (1 << GPIO_EN_OSC), ((en ? 1 : 0) << GPIO_EN_OSC));
}

int board_exm2pe_set_dac(board_exm2pe_t* brd, unsigned value)
{
    unsigned i2ca_dac = MAKE_LSOP_I2C_ADDR(LSOP_I2C_INSTANCE(brd->i2c_loc), LSOP_I2C_BUSNO(brd->i2c_loc), I2C_ADDR_DAC);
    if (!brd->dac_present)
        return -EACCES;

    USDR_LOG("M2PE", USDR_LOG_ERROR, "DAC set to: %d\n", value);
    return dac80501_dac_set(brd->dev, brd->subdev, i2ca_dac, value);
}
