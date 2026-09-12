// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include "conv_2ci16_ci12_2.h"
#include "attribute_switch.h"

#define TEMPLATE_FUNC_NAME conv_2ci16_ci12_generic
VWLT_ATTRIBUTE(optimize("-O3"))
#include "templates/conv_2ci16_ci12_generic.t"
DECLARE_TR_FUNC_2_1(conv_2ci16_ci12_generic)

#ifdef WVLT_AVX512BW
#define TEMPLATE_FUNC_NAME conv_2ci16_ci12_avx512bw
VWLT_ATTRIBUTE(optimize("-O3"), target("avx512bw"))
#include "templates/conv_2ci16_ci12_avx512bw.t"
DECLARE_TR_FUNC_2_1(conv_2ci16_ci12_avx512bw)
#endif

#ifdef WVLT_AVX2
#define TEMPLATE_FUNC_NAME conv_2ci16_ci12_avx2
VWLT_ATTRIBUTE(optimize("-O3"), target("avx2"))
#include "templates/conv_2ci16_ci12_avx2.t"
DECLARE_TR_FUNC_2_1(conv_2ci16_ci12_avx2)
#endif

#ifdef WVLT_NEON
#define TEMPLATE_FUNC_NAME conv_2ci16_ci12_neon
VWLT_ATTRIBUTE(optimize("-O3"))
#include "templates/conv_2ci16_ci12_neon.t"
DECLARE_TR_FUNC_2_1(conv_2ci16_ci12_neon)
#endif

conv_function_t conv_get_2ci16_ci12_c(generic_opts_t cpu_cap, const char** sfunc)
{
    const char* fname;
    conv_function_t fn;

    SELECT_GENERIC_FN(fn, fname, tr_conv_2ci16_ci12_generic, cpu_cap);
    SELECT_AVX2_FN(fn, fname, tr_conv_2ci16_ci12_avx2, cpu_cap);
    SELECT_AVX512BW_FN(fn, fname, tr_conv_2ci16_ci12_avx512bw, cpu_cap);
    SELECT_NEON_FN(fn, fname, tr_conv_2ci16_ci12_neon, cpu_cap);

    if (sfunc) *sfunc = fname;
    return fn;
}

conv_function_t conv_get_2ci16_ci12()
{
    return conv_get_2ci16_ci12_c(cpu_vcap_get(), NULL);
}
