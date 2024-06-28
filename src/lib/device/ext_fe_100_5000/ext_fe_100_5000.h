// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef EXT_FE_100_5000_H
#define EXT_FE_100_5000_H

#include <usdr_port.h>
#include <usdr_lowlevel.h>
#include <stdbool.h>
#include "../device.h"
#include "../device_vfs.h"


struct ext_fe_100_5000 {
    lldev_t dev;
    unsigned subdev;

    unsigned gpio_base;
    unsigned spi_cfg_base;
    unsigned spi_bus;

    // SPI address
    unsigned spi_xra;
    unsigned spi_lo1;
    unsigned spi_lo2;
    unsigned spi_attn;

    // FE Parameters
    unsigned band;

    unsigned gain; //Gain block selector
    unsigned ifsel;
    unsigned atten;

    unsigned lofreq_khz;
};
typedef struct ext_fe_100_5000 ext_fe_100_5000_t;

// High level interface
int ext_fe_100_5000_cmd_wr(ext_fe_100_5000_t* ob, uint32_t addr, uint32_t reg);
int ext_fe_100_5000_cmd_rd(ext_fe_100_5000_t* ob, uint32_t reg, uint32_t* preg);


int ext_fe_100_5000_init(lldev_t dev,
                         unsigned subdev,
                         unsigned gpio_base,
                         unsigned spi_cfg_base,
                         unsigned spi_bus,
                         const char *params,
                         const char *compat,
                         ext_fe_100_5000_t* ob);

int ext_fe_100_5000_set_frequency(ext_fe_100_5000_t* ob, uint64_t freq_hz);

int ext_fe_100_5000_set_attenuator(ext_fe_100_5000_t* ob, unsigned atten0_25db);

int ext_fe_100_5000_set_lna(ext_fe_100_5000_t* ob, unsigned lna);


#endif
