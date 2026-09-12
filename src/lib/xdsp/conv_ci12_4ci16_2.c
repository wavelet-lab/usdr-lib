// Copyright (c) 2025 Wavelet Lab
// SPDX-License-Identifier: MIT

#include "conv_ci12_4ci16_2.h"
#include "attribute_switch.h"

#define TEMPLATE_FUNC_NAME conv_ci12_4ci16_generic
VWLT_ATTRIBUTE(optimize("-O3"))
#include "templates/conv_ci12_4ci16_generic.t"
DECLARE_TR_FUNC_1_4(conv_ci12_4ci16_generic)

#ifdef WVLT_AVX512BW
#define TEMPLATE_FUNC_NAME conv_ci12_4ci16_avx512bw
VWLT_ATTRIBUTE(optimize("-O3"), target("avx512bw,avx512vl"))
#include "templates/conv_ci12_4ci16_avx512bw.t"
DECLARE_TR_FUNC_1_4(conv_ci12_4ci16_avx512bw)
#endif

#ifdef WVLT_AVX2
#define TEMPLATE_FUNC_NAME conv_ci12_4ci16_avx2
VWLT_ATTRIBUTE(optimize("-O3"), target("avx2"))
#include "templates/conv_ci12_4ci16_avx2.t"
DECLARE_TR_FUNC_1_4(conv_ci12_4ci16_avx2)
#endif

#ifdef WVLT_NEON
#define TEMPLATE_FUNC_NAME conv_ci12_4ci16_neon
VWLT_ATTRIBUTE(optimize("-O3"))
#include "templates/conv_ci12_4ci16_neon.t"
DECLARE_TR_FUNC_1_4(conv_ci12_4ci16_neon)
#endif

conv_function_t conv_get_ci12_4ci16_c(generic_opts_t cpu_cap, const char** sfunc)
{
    const char* fname;
    conv_function_t fn;

    SELECT_GENERIC_FN(fn, fname, tr_conv_ci12_4ci16_generic, cpu_cap);
    SELECT_AVX2_FN(fn, fname, tr_conv_ci12_4ci16_avx2, cpu_cap);
    SELECT_AVX512BW_FN(fn, fname, tr_conv_ci12_4ci16_avx512bw, cpu_cap);
    SELECT_NEON_FN(fn, fname, tr_conv_ci12_4ci16_neon, cpu_cap);

    if (sfunc) *sfunc = fname;
    return fn;
}

conv_function_t conv_get_ci12_4ci16()
{
    return conv_get_ci12_4ci16_c(cpu_vcap_get(), NULL);
}
