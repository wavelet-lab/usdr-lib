// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include "conv_ci16_2cf32_2.h"
#include "attribute_switch.h"

#define CONV_SCALE (1.0f/32767)

#define TEMPLATE_FUNC_NAME conv_ci16_2cf32_generic
VWLT_ATTRIBUTE(optimize("-O3"))
#include "templates/conv_ci16_2cf32_generic.t"
DECLARE_TR_FUNC_1_2(conv_ci16_2cf32_generic)

#ifdef WVLT_SSE2
#define TEMPLATE_FUNC_NAME conv_ci16_2cf32_sse2
VWLT_ATTRIBUTE(optimize("-O3"), target("sse2"))
#include "templates/conv_ci16_2cf32_sse2.t"
DECLARE_TR_FUNC_1_2(conv_ci16_2cf32_sse2)
#endif

#ifdef WVLT_AVX
#define TEMPLATE_FUNC_NAME conv_ci16_2cf32_avx
VWLT_ATTRIBUTE(optimize("-O3"), target("avx"))
#include "templates/conv_ci16_2cf32_sse2.t"
DECLARE_TR_FUNC_1_2(conv_ci16_2cf32_avx)
#endif

#ifdef WVLT_AVX2
#define TEMPLATE_FUNC_NAME conv_ci16_2cf32_avx2
VWLT_ATTRIBUTE(optimize("-O3"), target("avx2"))
#include "templates/conv_ci16_2cf32_avx2.t"
DECLARE_TR_FUNC_1_2(conv_ci16_2cf32_avx2)
#endif

#ifdef WVLT_NEON
#define TEMPLATE_FUNC_NAME conv_ci16_2cf32_neon
VWLT_ATTRIBUTE(optimize("-O3"))
#include "templates/conv_ci16_2cf32_neon.t"
DECLARE_TR_FUNC_1_2(conv_ci16_2cf32_neon)
#endif

conv_function_t conv_get_ci16_2cf32_c(generic_opts_t cpu_cap, const char** sfunc)
{
    const char* fname;
    conv_function_t fn;

    SELECT_GENERIC_FN(fn, fname, tr_conv_ci16_2cf32_generic, cpu_cap);
    SELECT_SSE2_FN(fn, fname, tr_conv_ci16_2cf32_sse2, cpu_cap);
    SELECT_AVX_FN(fn, fname, tr_conv_ci16_2cf32_avx, cpu_cap);
    SELECT_AVX2_FN(fn, fname, tr_conv_ci16_2cf32_avx2, cpu_cap);
    SELECT_NEON_FN(fn, fname, tr_conv_ci16_2cf32_neon, cpu_cap);

    if (sfunc) *sfunc = fname;
    return fn;
}

conv_function_t conv_get_ci16_2cf32()
{
    return conv_get_ci16_2cf32_c(cpu_vcap_get(), NULL);
}
