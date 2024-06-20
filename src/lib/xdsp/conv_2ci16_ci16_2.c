// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include "conv_2ci16_ci16_2.h"
#include "attribute_switch.h"

#define TEMPLATE_FUNC_NAME conv_2ci16_ci16_generic
VWLT_ATTRIBUTE(optimize("-O3"))
#include "templates/conv_2ci16_ci16_generic.t"
DECLARE_TR_FUNC_2_1(conv_2ci16_ci16_generic)

#if 0
#ifdef WVLT_SSE2
#define TEMPLATE_FUNC_NAME conv_2ci16_ci16_sse2
VWLT_ATTRIBUTE(optimize("-O3"), target("sse2"))
#include "templates/conv_2ci16_ci16_generic.t"
DECLARE_TR_FUNC_2_1(conv_2ci16_ci16_sse2)
#endif

#ifdef WVLT_AVX
#define TEMPLATE_FUNC_NAME conv_2ci16_ci16_avx
VWLT_ATTRIBUTE(optimize("-O3"), target("avx"))
#include "templates/conv_2ci16_ci16_generic.t"
DECLARE_TR_FUNC_2_1(conv_2ci16_ci16_avx)
#endif
#endif

#ifdef WVLT_AVX2
#define TEMPLATE_FUNC_NAME conv_2ci16_ci16_avx2
VWLT_ATTRIBUTE(optimize("-O3"), target("avx2"))
#include "templates/conv_2ci16_ci16_avx2.t"
DECLARE_TR_FUNC_2_1(conv_2ci16_ci16_avx2)
#endif

conv_function_t conv_get_2ci16_ci16_c(generic_opts_t cpu_cap, const char** sfunc)
{
    const char* fname;
    conv_function_t fn;

    SELECT_GENERIC_FN(fn, fname, tr_conv_2ci16_ci16_generic, cpu_cap);
#if 0
    SELECT_SSE2_FN(fn, fname, tr_conv_2ci16_ci16_sse2, cpu_cap);
    SELECT_AVX_FN(fn, fname, tr_conv_2ci16_ci16_avx, cpu_cap);
#endif
    SELECT_AVX2_FN(fn, fname, tr_conv_2ci16_ci16_avx2, cpu_cap);

    if (sfunc) *sfunc = fname;
    return fn;
}

conv_function_t conv_get_2ci16_ci16()
{
    return conv_get_2ci16_ci16_c(cpu_vcap_get(), NULL);
}
