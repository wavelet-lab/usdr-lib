// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef EXT_SUPERSYNC_H
#define EXT_SUPERSYNC_H

#include <usdr_port.h>
#include <usdr_lowlevel.h>
#include <stdbool.h>
#include "../device.h"
#include "../device_vfs.h"

#include "../hw/lmk5c33216/lmk5c33216.h"


typedef int (*ext_i2c_func_t)(lldev_t dev, subdev_t subdev, unsigned ls_op, lsopaddr_t ls_op_addr,
                              size_t meminsz, void* pin, size_t memoutsz,
                              const void* pout);

struct board_ext_supersync {
    l5c33216_state_t lmk;

    ext_i2c_func_t func;
};

typedef struct board_ext_supersync board_ext_supersync_t;

int board_ext_supersync_init(lldev_t dev,
                             unsigned subdev,
                             unsigned gpio_base,
                             ext_i2c_func_t func,
                             board_ext_supersync_t* ob);


// int lmk_5c33216_reg_wr(ext_i2c_func_t f, lldev_t dev, subdev_t subdev, lsopaddr_t ls_op_addr,
//                        uint16_t reg, uint8_t out);

// int lmk_5c33216_reg_rd(ext_i2c_func_t f, lldev_t dev, subdev_t subdev, lsopaddr_t ls_op_addr,
//                        uint16_t reg, uint8_t* val);

#endif
