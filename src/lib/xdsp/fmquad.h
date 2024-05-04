// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef FMQUAD_H
#define FMQUAD_H

#include <stdint.h>


typedef struct quadfm_decode_state {
    int16_t iq_prev[2];
    float d_mp;
} quadfm_decode_state_t;


float quadfm_encode(unsigned samples,
                    const int16_t* audio,
                    int16_t* iq,
                    float gain,
                    float iangle);

int quadfm_decode(quadfm_decode_state_t* state,
                  const int16_t* piq,
                  unsigned samples,
                  int16_t* out,
                  int32_t* omaxp,
                  int64_t* opwr);

#endif
