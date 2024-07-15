// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef LMK5C33216_H
#define LMK5C33216_H

#include <usdr_lowlevel.h>

struct l5c33216_state {
    lldev_t dev;
    unsigned subdev;
    unsigned lsaddr;
};
typedef struct l5c33216_state l5c33216_state_t;


int lmk5c33216_create(lldev_t dev, unsigned subdev, unsigned lsaddr, l5c33216_state_t* out);


int lmk_5c33216_reg_wr(l5c33216_state_t* d, uint16_t reg, uint8_t out);
int lmk_5c33216_reg_rd(l5c33216_state_t* d, uint16_t reg, uint8_t* val);


#endif
