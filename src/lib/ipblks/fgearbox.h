// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef FGEARBOX_H
#define FGEARBOX_H

#include <usdr_lowlevel.h>
#include <usdr_logging.h>

enum fgearbox_firs {
    FGBOX_X2 = 2,
    FGBOX_X128 = 128,

};

typedef enum fgearbox_firs fgearbox_firs_t;


int fgearbox_load_fir(lldev_t dev, unsigned gport, fgearbox_firs_t fir);


#endif
