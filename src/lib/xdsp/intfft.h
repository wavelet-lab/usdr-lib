// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef INTFFT_H
#define INTFFT_H

#include <stdint.h>

struct fft_plane {
    unsigned n;
    unsigned tw2;
    uint8_t normstages[16];
    int16_t* buffer;
    int16_t* factors;
};




#endif
