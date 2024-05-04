// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef TRIG_H
#define TRIG_H

#include <stdint.h>

// Validation function
// phase [-pi/2; pi/2) mmaped to [-32768; 32767]
// output [-1; 1] mmaped to [-32767; 32767]
void isincos_gen86(const int16_t *ph, int16_t* psin, int16_t *pcos);

#define CORR_32768
#endif
