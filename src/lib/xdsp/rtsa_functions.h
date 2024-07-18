// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef RTSA_FUNCTIONS_H
#define RTSA_FUNCTIONS_H

#include <stdint.h>
#include <math.h>
#include <assert.h>
#include "conv.h"

#define CHARGE_NORM_COEF     (float)MAX_RTSA_PWR * M_E / (M_E - 1)
#define DISCHARGE_NORM_COEF  (float)MAX_RTSA_PWR / (M_E - 1)

static inline void rtsa_calc_depth(fft_rtsa_settings_t* st)
{
    assert(st->upper_pwr_bound > st->lower_pwr_bound);
    unsigned depth = (st->upper_pwr_bound - st->lower_pwr_bound) * st->divs_for_dB;

    // need to align rtsa_depth for proper SIMD processing
    const unsigned align_bytes = cpu_vcap_align(cpu_vcap_get());
    const unsigned d = align_bytes / sizeof(rtsa_pwr_t);

    st->rtsa_depth = d * ceil((float)depth / (float)d);
    st->lower_pwr_bound = st->upper_pwr_bound - st->rtsa_depth / st->divs_for_dB;
}

#ifdef __cplusplus
extern "C" {
#endif

void rtsa_init(fft_rtsa_data_t* rtsa_data, unsigned fft_size);

rtsa_update_function_t rtsa_update_c(generic_opts_t cpu_cap, const char** sfunc);
rtsa_update_hwi16_function_t rtsa_update_hwi16_c(generic_opts_t cpu_cap, const char** sfunc);

static inline
void rtsa_update(wvlt_fftwf_complex* in, unsigned fft_size,
                 fft_rtsa_data_t* rtsa_data,
                 float fcale_mpy, float mine, float corr)
{
    return (*rtsa_update_c(cpu_vcap_get(), NULL)) (in, fft_size, rtsa_data, fcale_mpy, mine, corr);
}

static inline
void rtsa_update_hwi16(uint16_t* in, unsigned fft_size,
                       fft_rtsa_data_t* rtsa_data,
                       float fcale_mpy, float corr)
{
    return (*rtsa_update_hwi16_c(cpu_vcap_get(), NULL)) (in, fft_size, rtsa_data, fcale_mpy, corr);
}

#ifdef __cplusplus
}
#endif

#endif // RTSA_FUNCTIONS_H
