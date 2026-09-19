// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef GPIO_H
#define GPIO_H

#include <usdr_port.h>
#include <usdr_lowlevel.h>
#include <stdbool.h>

enum gpio_config_vals {
    GPIO_CFG_IN = 0,
    GPIO_CFG_OUT = 1,
    GPIO_CFG_ALT0 = 2,
    GPIO_CFG_ALT1 = 3,
};

// Core supports gpio0 through gpio14
enum gpio_ports {
    GPIO0 = 0,
    GPIO1 = 1,
    GPIO2 = 2,
    GPIO3 = 3,
    GPIO4 = 4,
    GPIO5 = 5,
    GPIO6 = 6,
    GPIO7 = 7,

    GPIO8 = 8,
    GPIO9 = 9,
    GPIO10 = 10,
    GPIO11 = 11,
    GPIO12 = 12,
    GPIO13 = 13,
    GPIO14 = 14,
};

int gpio_config(lldev_t lldev,
                subdev_t subdev,
                unsigned base,
                unsigned portno,
                unsigned cfg);

enum gpio_cmds {
    GPIO_OUT = 0,
    GPIO_SET = 1,
    GPIO_CLR = 2,
};

int gpio_cmd(lldev_t lldev,
             subdev_t subdev,
             unsigned base,
             unsigned cmd,
             unsigned mask,
             unsigned value);

int gpio_in(lldev_t lldev,
             subdev_t subdev,
             unsigned base,
             unsigned *value);


#endif
