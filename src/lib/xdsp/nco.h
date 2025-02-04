// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef NCO_H
#define NCO_H

#include <stdint.h>
#include "conv.h"


conv_function_t get_wvlt_sincos_i16_c(generic_opts_t cpu_cap, const char** sfunc);
conv_function_t get_wvlt_sincos_i16();

static inline
void wvlt_sincos_i16(const int16_t* phase, unsigned phase_len, int16_t* sindata, int16_t* cosdata)
{
    void* out[2] = {sindata, cosdata};
    const unsigned bsize = phase_len * sizeof(int16_t);
    return (*get_wvlt_sincos_i16())((const void**)&phase, bsize, out, bsize);
}


int32_t nco_shift(int32_t inphase,
                  int32_t delta,
                  const int16_t* iqbuf,
                  unsigned csamples,
                  int16_t* out);

#endif
