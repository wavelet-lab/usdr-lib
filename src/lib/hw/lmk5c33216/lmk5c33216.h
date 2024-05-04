// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef LMK5C33216_H
#define LMK5C33216_H

#include <usdr_lowlevel.h>

typedef int (*ext_i2c_func_t)(lldev_t dev, subdev_t subdev, unsigned ls_op, lsopaddr_t ls_op_addr,
                              size_t meminsz, void* pin, size_t memoutsz,
                              const void* pout);



struct l5c33216_state {
    lldev_t dev;
    unsigned subdev;
    unsigned lsaddr;
    ext_i2c_func_t i2c_func;
};
typedef struct l5c33216_state l5c33216_state_t;


int lmk5c33216_create(lldev_t dev, unsigned subdev, unsigned lsaddr, ext_i2c_func_t func, l5c33216_state_t* out);


int lmk_5c33216_reg_wr(l5c33216_state_t* d, uint16_t reg, uint8_t out);
int lmk_5c33216_reg_rd(l5c33216_state_t* d, uint16_t reg, uint8_t* val);


#endif
