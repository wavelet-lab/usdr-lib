// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <stdint.h>
#include <math.h>
#include "fmquad.h"
#include "attribute_switch.h"


VWLT_ATTRIBUTE(optimize("O3"))
float quadfm_encode(unsigned samples,
                    const int16_t* audio,
                    int16_t* iq,
                    float gain,
                    float iangle)
{
    unsigned i;
    for (i = 0; i < samples; i++ ) {
        float da = audio[i] * gain;
        float fi, fq;

        iangle += da;
        sincosf(iangle, &fq, &fi);

        int16_t vi, vq;
        vi = (int16_t)(fi * 0.7f * 32767.0f);
        vq = (int16_t)(fq * 0.7f * 32767.0f);

        iq[2 * i + 0] = vi;
        iq[2 * i + 1] = vq;
    }
    return iangle;
}

VWLT_ATTRIBUTE(optimize("O3"))
int quadfm_decode(quadfm_decode_state_t* state,
                  const int16_t* piq,
                  unsigned samples,
                  int16_t* out,
                  int32_t* omaxp,
                  int64_t* opwr)
{
    unsigned i;
    int64_t tpwr = 0;

    int16_t iq_prev[2] = { state->iq_prev[0], state->iq_prev[1] };
    int32_t pwr;
    int32_t maxp = 0;
    int16_t iq[2];
    int32_t ld[2];

    int16_t o;

    for (i = 0; i < samples; i++ ) {
        iq[0] = piq[2 * i];
        iq[1] = piq[2 * i + 1];

        // Calc PWR
        pwr = iq[0] * iq[0] + iq[1] * iq[1];

        tpwr += pwr;
        if (maxp < pwr)
            maxp = pwr;

        // Calc differential of x(0) and x*(-1)
        ld[0] = (int32_t)iq[0] * (int32_t)iq_prev[0] + (int32_t)iq[1] * (int32_t)iq_prev[1];
        ld[1] = (int32_t)iq[1] * (int32_t)iq_prev[0] - (int32_t)iq[0] * (int32_t)iq_prev[1];

        // decode & multiply
        o = (int16_t)(atan2f(ld[1], ld[0]) * state->d_mp);

        iq_prev[0] = iq[0];
        iq_prev[1] = iq[1];
        out[i] = o;
    }

    state->iq_prev[0] = iq_prev[0];
    state->iq_prev[1] = iq_prev[1];

    *omaxp = maxp;
    *opwr = tpwr;
    return 0;
}

