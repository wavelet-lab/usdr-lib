// Copyright (c) 2025 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef SINCOS_FUNCTIONS_H
#define SINCOS_FUNCTIONS_H

#include <stdint.h>
#include "conv.h"

#define WVLT_SINCOS_I16_SCALE 32767
#define WVLT_SINCOS_I32_PHSCALE (M_PI / INT32_MAX)
#define WVLT_SINCOS_I16_PHSCALE (M_PI / (2 * INT16_MAX))


conv_function_t get_wvlt_sincos_i16_c(generic_opts_t cpu_cap, const char** sfunc);
conv_function_t get_wvlt_sincos_i16();

static inline
    void wvlt_sincos_i16(const int16_t* phase, unsigned phase_len, int16_t* sindata, int16_t* cosdata)
{
    void* out[2] = {sindata, cosdata};
    const unsigned bsize = phase_len * sizeof(int16_t);
    return (*get_wvlt_sincos_i16())((const void**)&phase, bsize, out, bsize);
}


sincos_i16_interleaved_ctrl_function_t get_wvlt_sincos_i16_interleaved_ctrl_c(generic_opts_t cpu_cap, const char** sfunc);
sincos_i16_interleaved_ctrl_function_t get_wvlt_sincos_i16_interleaved_ctrl();

/*
 * wvlt_sincos_i16_interleaved_ctrl()
 *
 * int32_t* start_phase: Starting phase.
 *                       Diapazon 0..2*PI mapped to int32 range [0..131072).
 *                       Must be 0 or positive.
 *                       The next starting phase for consequent calls is returned by ptr.
 * int32_t delta_phase:  Delta, applying to starting phase. int32_t range is just the same as for start_phase.
 *                       Must be 0 or positive.
 * bool inv_sin:         Invert result sin values
 * bool inv_cos:         Invert result cos values
 * int16_t* outdata:     Array of output data. Format is interleaved sin/cos int16_t pairs.
 *                       Sin & cos values are within (-32768..32767] range.
 * unsigned iters:       Iterations count. One iteration == sin+cos pair calculation for some phase, and phase incrementation.
 */
static inline
    void wvlt_sincos_i16_interleaved_ctrl(int32_t* start_phase, int32_t delta_phase,
                                     bool inv_sin, bool inv_cos,
                                     int16_t* outdata,
                                     unsigned iters)
{
    return (*get_wvlt_sincos_i16_interleaved_ctrl())(start_phase, delta_phase, inv_sin, inv_cos, outdata, iters);
}

#endif // SINCOS_FUNCTIONS_H
