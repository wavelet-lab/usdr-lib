// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef BOARD_EXM2PE_H
#define BOARD_EXM2PE_H

#include <usdr_port.h>
#include <usdr_lowlevel.h>
#include <stdbool.h>
#include "../device.h"
#include "../device_vfs.h"


struct board_exm2pe {
    lldev_t dev;
    unsigned subdev;
    unsigned gpio_base;
    unsigned i2c_loc;

    bool dac_present;
};

typedef struct board_exm2pe board_exm2pe_t;

int board_exm2pe_init(lldev_t dev,
                      unsigned subdev,
                      unsigned gpio_base,
                      unsigned int uart_base,
                      const char *params,
                      const char *compat,
                      unsigned i2c_loc,
                      board_exm2pe_t* ob);

int board_exm2pe_enable_gps(board_exm2pe_t* brd, bool en);
int board_exm2pe_enable_osc(board_exm2pe_t* brd, bool en);
int board_exm2pe_set_dac(board_exm2pe_t* brd, unsigned value);



#endif
