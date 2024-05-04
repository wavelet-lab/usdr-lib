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
                             ext_i2c_func_t func,
                             board_ext_simplesync_t* ob);



//int lmk_05318b_reg_wr(ext_i2c_func_t f, lldev_t dev, subdev_t subdev, lsopaddr_t ls_op_addr,
//                              uint16_t reg, uint8_t out);

//int lmk_05318b_reg_rd(ext_i2c_func_t f, lldev_t dev, subdev_t subdev, lsopaddr_t ls_op_addr,
//                   uint16_t reg, uint8_t* val);

#endif
