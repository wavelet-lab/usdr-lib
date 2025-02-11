// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef NCO_H
#define NCO_H

#include <stdint.h>

int32_t nco_shift(int32_t inphase,
                  int32_t delta,
                  const int16_t* iqbuf,
                  unsigned csamples,
                  int16_t* out);

#endif
