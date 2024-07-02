// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include <string.h>
#include <stdio.h>
#include <assert.h>

#include "rtsa_functions.h"
#include "attribute_switch.h"
#include "fast_math.h"


#define USE_ACCURATE_LOG2

VWLT_ATTRIBUTE(optimize("-O3"))
void rtsa_init(fft_rtsa_data_t* rtsa_data, unsigned fft_size)
{
    memset(rtsa_data->pwr, 0, sizeof(rtsa_pwr_t) * rtsa_data->settings.rtsa_depth * fft_size);
}

#define TEMPLATE_FUNC_NAME rtsa_update_generic
#include "templates/rtsa_update_u16_generic.t"
DECLARE_TR_FUNC_RTSA_UPDATE(rtsa_update_generic)

#ifdef WVLT_AVX2
#define TEMPLATE_FUNC_NAME rtsa_update_avx2
VWLT_ATTRIBUTE(optimize("-O3"), target("avx2,fma"))
#include "templates/rtsa_update_u16_avx2.t"
DECLARE_TR_FUNC_RTSA_UPDATE(rtsa_update_avx2)
#endif  //WVLT_AVX2

#ifdef WVLT_NEON
#define TEMPLATE_FUNC_NAME rtsa_update_neon
VWLT_ATTRIBUTE(optimize("-O3"))
#include "templates/rtsa_update_neon_u16.t"
DECLARE_TR_FUNC_RTSA_UPDATE(rtsa_update_neon)
#endif  //WVLT_NEON

rtsa_update_function_t rtsa_update_c(generic_opts_t cpu_cap, const char** sfunc)
{
    const char* fname;
    rtsa_update_function_t fn;

    SELECT_GENERIC_FN(fn, fname, tr_rtsa_update_generic, cpu_cap);
    SELECT_AVX2_FN(fn, fname, tr_rtsa_update_avx2, cpu_cap);
    SELECT_NEON_FN(fn, fname, tr_rtsa_update_neon, cpu_cap);

    if (sfunc) *sfunc = fname;
    return fn;
}

