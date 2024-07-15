// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef EXT_SIMPLESYNC_H
#define EXT_SIMPLESYNC_H

#include <usdr_port.h>
#include <usdr_lowlevel.h>
#include <stdbool.h>
#include "../device.h"
#include "../device_vfs.h"

#include "../hw/lmk05318/lmk05318.h"

struct board_ext_simplesync {
    lmk05318_state_t lmk;

    unsigned gpio_base;
};
typedef struct board_ext_simplesync board_ext_simplesync_t;

int simplesync_tune_lo(board_ext_simplesync_t* ob, uint32_t meas_lo);

int board_ext_simplesync_init(lldev_t dev,
                              unsigned subdev,
                              unsigned gpio_base,
                              const char* compat,
                              unsigned int i2c_loc,
                              board_ext_simplesync_t* ob);


#endif
