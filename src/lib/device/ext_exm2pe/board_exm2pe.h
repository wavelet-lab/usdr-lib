// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef BOARD_EXM2PE_H
#define BOARD_EXM2PE_H

#include <usdr_port.h>
#include <usdr_lowlevel.h>
#include <stdbool.h>
#include "../device.h"
#include "../device_vfs.h"


typedef int (*ext_i2c_func_t)(lldev_t dev, subdev_t subdev, unsigned ls_op, lsopaddr_t ls_op_addr,
                              size_t meminsz, void* pin, size_t memoutsz,
                              const void* pout);

struct board_exm2pe {
    lldev_t dev;
    unsigned subdev;
    unsigned gpio_base;
    ext_i2c_func_t func;

    bool dac_present;
};

typedef struct board_exm2pe board_exm2pe_t;

int board_exm2pe_init(lldev_t dev,
                      unsigned subdev,
                      unsigned gpio_base,
                      const char *params,
                      const char *compat,
                      ext_i2c_func_t func,
                      board_exm2pe_t* ob);

int board_exm2pe_enable_gps(board_exm2pe_t* brd, bool en);
int board_exm2pe_enable_osc(board_exm2pe_t* brd, bool en);
int board_exm2pe_set_dac(board_exm2pe_t* brd, unsigned value);



#endif
