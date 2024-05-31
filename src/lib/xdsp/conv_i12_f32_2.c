// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include "conv_i12_f32_2.h"
#include "attribute_switch.h"

#define CONV_SCALE (1.0f/32767)

#define TEMPLATE_FUNC_NAME conv_i12_f32_generic
VWLT_ATTRIBUTE(optimize("-O3"))
#include "templates/conv_i12_f32_generic.t"
DECLARE_TR_FUNC_1_1(conv_i12_f32_generic)

#if 0
#ifdef WVLT_SSE2
#define TEMPLATE_FUNC_NAME conv_i12_f32_sse2
VWLT_ATTRIBUTE(optimize("-O3"), target("sse2"))
#include "templates/conv_i12_f32_generic.t"
DECLARE_TR_FUNC_1_1(conv_i12_f32_sse2)
#endif

#ifdef WVLT_AVX
#define TEMPLATE_FUNC_NAME conv_i12_f32_avx
VWLT_ATTRIBUTE(optimize("-O3"), target("avx"))
#include "templates/conv_i12_f32_generic.t"
DECLARE_TR_FUNC_1_1(conv_i12_f32_avx)
#endif
#endif

#ifdef WVLT_SSSE3
#define TEMPLATE_FUNC_NAME conv_i12_f32_ssse3
VWLT_ATTRIBUTE(optimize("-O3"), target("ssse3"))
#include "templates/conv_i12_f32_ssse3.t"
DECLARE_TR_FUNC_1_1(conv_i12_f32_ssse3)
#endif

conv_function_t conv_get_i12_f32_c(generic_opts_t cpu_cap, const char** sfunc)
{
    const char* fname;
    conv_function_t fn;

    SELECT_GENERIC_FN(fn, fname, tr_conv_i12_f32_generic, cpu_cap);
    SELECT_SSSE3_FN(fn, fname, tr_conv_i12_f32_ssse3, cpu_cap);
#if 0
    SELECT_SSE2_FN(fn, fname, tr_conv_i12_f32_sse2, cpu_cap);
    SELECT_AVX_FN(fn, fname, tr_conv_i12_f32_avx, cpu_cap);
#endif
    if (sfunc) *sfunc = fname;
    return fn;
}

conv_function_t conv_get_i12_f32()
{
    return conv_get_i12_f32_c(cpu_vcap_get(), NULL);
}
