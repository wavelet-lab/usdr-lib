// Copyright (c) 2025 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef LMX1204_H
#define LMX1204_H

#include <usdr_lowlevel.h>

struct lmx1204_state
{
    lldev_t dev;
    unsigned subdev;
    unsigned lsaddr;

};
typedef struct lmx1204_state lmx1204_state_t;

int lmx1204_get_temperature(lmx1204_state_t* st, float* value);
int lmx1204_create(lldev_t dev, unsigned subdev, unsigned lsaddr, lmx1204_state_t* st);
int lmx1204_destroy(lmx1204_state_t* st);


#endif // LMX1204_H
