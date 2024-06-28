// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef LMS8001_H
#define LMS8001_H

// LMS8001 control logic mostly for block specific perfective
#include <stdint.h>
#include <usdr_lowlevel.h>


struct lms8001_state {
    lldev_t dev;
    unsigned subdev;
    unsigned lsaddr;

    // Enabled channel masks
    unsigned chan_mask;
};
typedef struct lms8001_state lms8001_state_t;


int lms8001_create(lldev_t dev, unsigned subdev, unsigned lsaddr, lms8001_state_t *out);
int lms8001_destroy(lms8001_state_t* m);

int lms8001_tune(lms8001_state_t* state, unsigned fref, uint64_t out);
int lms8001_ch_enable(lms8001_state_t* state, unsigned mask);

#endif
