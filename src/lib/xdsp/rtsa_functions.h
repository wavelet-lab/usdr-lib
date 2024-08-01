// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef RTSA_FUNCTIONS_H
#define RTSA_FUNCTIONS_H

#include <stdint.h>
#include <math.h>
#include <assert.h>
#include "conv.h"
#include "fast_math.h"

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

static inline void rtsa_fill_hwi16_consts(const fft_rtsa_settings_t* st, unsigned fft_size, float scale, rtsa_hwi16_consts_t* c)
{
    c->org_scale = scale * HWI16_SCALE_COEF;
    const uint16_t nscale = (uint16_t)(wvlt_polylog2f(c->org_scale) + 0.5);

    c->nfft         = (uint16_t)wvlt_polylog2f(fft_size);
    c->ndivs_for_dB = (uint16_t)(wvlt_polylog2f(st->divs_for_dB) + 0.5f);
    c->c0           = (uint16_t)(2 * HWI16_SCALE_COEF * c->nfft);
    c->c1           = ((uint16_t)(- HWI16_CORR_COEF * ( 1.f - 1.f / wvlt_polylog2f(10))) << c->ndivs_for_dB) + st->upper_pwr_bound;
    c->shr0         = nscale;
    c->shr1         = (uint16_t)(HWI16_SCALE_N2_COEF - nscale > c->ndivs_for_dB ? HWI16_SCALE_N2_COEF - nscale - c->ndivs_for_dB : 16);
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
                 float fcale_mpy, float mine, float corr, fft_diap_t diap)
{
    return (*rtsa_update_c(cpu_vcap_get(), NULL)) (in, fft_size, rtsa_data, fcale_mpy, mine, corr, diap);
}

static inline
void rtsa_update_hwi16(uint16_t* in, unsigned fft_size,
                       fft_rtsa_data_t* rtsa_data,
                       float fcale_mpy, float corr, fft_diap_t diap, const rtsa_hwi16_consts_t* hwi16_consts)
{
    return (*rtsa_update_hwi16_c(cpu_vcap_get(), NULL)) (in, fft_size, rtsa_data, fcale_mpy, corr, diap, hwi16_consts);
}

#ifdef __cplusplus
}
#endif

#endif // RTSA_FUNCTIONS_H
