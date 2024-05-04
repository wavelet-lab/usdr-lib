// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include "conv_i16_f32_2.h"
#include <stddef.h>
#include "attribute_switch.h"

#define CONV_SCALE (1.0f/32767)

#define TEMPLATE_FUNC_NAME conv_i16_f32_generic
VWLT_ATTRIBUTE(optimize("-O3"))
#include "templates/conv_i16_f32_generic.t"
DECLARE_TR_FUNC_1_1(conv_i16_f32_generic)

#ifdef __SSE2__
#define TEMPLATE_FUNC_NAME conv_i16_f32_sse2
VWLT_ATTRIBUTE(optimize("-O3"), target("sse2"))
#include "templates/conv_i16_f32_sse2.t"
DECLARE_TR_FUNC_1_1(conv_i16_f32_sse2)
#endif

#ifdef __AVX__
#define TEMPLATE_FUNC_NAME conv_i16_f32_avx
VWLT_ATTRIBUTE(optimize("-O3"), target("avx"))
#include "templates/conv_i16_f32_sse2.t"
DECLARE_TR_FUNC_1_1(conv_i16_f32_avx)
#endif

#ifdef __AVX2__
#define TEMPLATE_FUNC_NAME conv_i16_f32_avx2
VWLT_ATTRIBUTE(optimize("-O3"), target("avx2"))
#include "templates/conv_i16_f32_avx2.t"
DECLARE_TR_FUNC_1_1(conv_i16_f32_avx2)
#endif

#ifdef WVLT_ARCH_ARM64
#define TEMPLATE_FUNC_NAME conv_i16_f32_neon
VWLT_ATTRIBUTE(optimize("-O3"))
#include "templates/conv_i16_f32_neon.t"
DECLARE_TR_FUNC_1_1(conv_i16_f32_neon)
#endif

conv_function_t conv_get_i16_f32_c(generic_opts_t cpu_cap, const char** sfunc)
{
    const char* fname;
    conv_function_t fn;

    SELECT_GENERIC_FN(fn, fname, tr_conv_i16_f32_generic, cpu_cap);
    SELECT_SSE2_FN(fn, fname, tr_conv_i16_f32_sse2, cpu_cap);
    SELECT_AVX_FN(fn, fname, tr_conv_i16_f32_avx, cpu_cap);
    SELECT_AVX2_FN(fn, fname, tr_conv_i16_f32_avx2, cpu_cap);
    SELECT_NEON_FN(fn, fname, tr_conv_i16_f32_neon, cpu_cap);

    if (sfunc) *sfunc = fname;
    return fn;
}

conv_function_t conv_get_i16_f32()
{
    return conv_get_i16_f32_c(cpu_vcap_get(), NULL);
}
