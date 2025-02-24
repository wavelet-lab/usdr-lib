// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef FGEARBOX_H
#define FGEARBOX_H

#include <usdr_lowlevel.h>
#include <usdr_logging.h>

enum fgearbox_firs {
    FGBOX_BP = 1,

    FGBOX_X2 = 2,
    FGBOX_X3 = 3,
    FGBOX_X4 = 4,
    FGBOX_X5 = 5,
    FGBOX_X6 = 6,

    FGBOX_X8 = 8,
    FGBOX_X12 = 12,
    FGBOX_X16 = 16,
    FGBOX_X24 = 24,
    FGBOX_X32 = 32,
    FGBOX_X48 = 48,
    FGBOX_X64 = 64,
    FGBOX_X128 = 128,
    FGBOX_X256 = 256,

};

typedef enum fgearbox_firs fgearbox_firs_t;


int fgearbox_load_fir(lldev_t dev, unsigned gport, fgearbox_firs_t fir);

int fgearbox_load_fir_i(lldev_t dev, unsigned gport, fgearbox_firs_t fir);

#endif
