// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include "conv_f32_i16_2.h"
#include "attribute_switch.h"

#define CONV_SCALE (1.0f/32767)

#define TEMPLATE_FUNC_NAME conv_f32_i16_generic
VWLT_ATTRIBUTE(optimize("-O3"))
#include "templates/conv_f32_i16_generic.t"
DECLARE_TR_FUNC_1_1(conv_f32_i16_generic)

#ifdef WVLT_SSE2
#define TEMPLATE_FUNC_NAME conv_f32_i16_sse2
VWLT_ATTRIBUTE(optimize("-O3"), target("sse2"))
#include "templates/conv_f32_i16_generic.t"
DECLARE_TR_FUNC_1_1(conv_f32_i16_sse2)
#endif

#ifdef WVLT_AVX
#define TEMPLATE_FUNC_NAME conv_f32_i16_avx
VWLT_ATTRIBUTE(optimize("-O3"), target("avx"))
#include "templates/conv_f32_i16_generic.t"
DECLARE_TR_FUNC_1_1(conv_f32_i16_avx)
#endif



conv_function_t conv_get_f32_i16_c(generic_opts_t cpu_cap, const char** sfunc)
{
    const char* fname;
    conv_function_t fn;

    SELECT_GENERIC_FN(fn, fname, tr_conv_f32_i16_generic, cpu_cap);
    SELECT_SSE2_FN(fn, fname, tr_conv_f32_i16_sse2, cpu_cap);
    SELECT_AVX_FN(fn, fname, tr_conv_f32_i16_avx, cpu_cap);

    if (sfunc) *sfunc = fname;
    return fn;
}

conv_function_t conv_get_f32_i16()
{
    return conv_get_f32_i16_c(cpu_vcap_get(), NULL);
}
