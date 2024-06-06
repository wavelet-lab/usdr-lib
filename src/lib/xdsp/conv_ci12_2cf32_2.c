// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include "conv_ci12_2cf32_2.h"
#include "attribute_switch.h"

#define CONV_SCALE (1.0f/32767)
#define SCALE2    (CONV_SCALE / 65536)

#define IQ12_SC32_SSSE3_EX_LOGIC
#define UNALIGN_STORE

#ifdef UNALIGN_STORE
#define _MM_STOREX_PS    _mm_storeu_ps
#define _MM256_STOREX_PS _mm256_storeu_ps
#else
#define _MM_STOREX_PS    _mm_store_ps
#define _MM256_STOREX_PS _mm256_store_ps
#endif

#define TEMPLATE_FUNC_NAME conv_ci12_2cf32_generic
VWLT_ATTRIBUTE(optimize("-O3"))
#include "templates/conv_ci12_2cf32_generic.t"
DECLARE_TR_FUNC_1_2(conv_ci12_2cf32_generic)

#ifdef WVLT_SSSE3
#define TEMPLATE_FUNC_NAME conv_ci12_2cf32_ssse3
VWLT_ATTRIBUTE(optimize("-O3"), target("ssse3"))
#include "templates/conv_ci12_2cf32_ssse3.t"
DECLARE_TR_FUNC_1_2(conv_ci12_2cf32_ssse3)
#endif

#if 0
#ifdef WVLT_AVX
#define TEMPLATE_FUNC_NAME conv_ci12_2cf32_avx
VWLT_ATTRIBUTE(optimize("-O3"), target("avx"))
#include "templates/conv_ci12_2cf32_generic.t"
DECLARE_TR_FUNC_1_2(conv_ci12_2cf32_avx)
#endif
#endif


conv_function_t conv_get_ci12_2cf32_c(generic_opts_t cpu_cap, const char** sfunc)
{
    const char* fname;
    conv_function_t fn;

    SELECT_GENERIC_FN(fn, fname, tr_conv_ci12_2cf32_generic, cpu_cap);
    SELECT_SSSE3_FN(fn, fname, tr_conv_ci12_2cf32_ssse3, cpu_cap);
#if 0
    SELECT_AVX_FN(fn, fname, tr_conv_ci12_2cf32_avx, cpu_cap);
#endif
    if (sfunc) *sfunc = fname;
    return fn;
}

conv_function_t conv_get_ci12_2cf32()
{
    return conv_get_ci12_2cf32_c(cpu_vcap_get(), NULL);
}
