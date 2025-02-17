// Copyright (c) 2025 Wavelet Lab
// SPDX-License-Identifier: MIT

#include "conv_ci16_4cf32_2.h"
#include "attribute_switch.h"

#define CONV_SCALE (1.0f/32767)

#define TEMPLATE_FUNC_NAME conv_ci16_4cf32_generic
VWLT_ATTRIBUTE(optimize("-O3"))
#include "templates/conv_ci16_4cf32_generic.t"
DECLARE_TR_FUNC_1_4(conv_ci16_4cf32_generic)

#ifdef WVLT_AVX2
#define TEMPLATE_FUNC_NAME conv_ci16_4cf32_avx2
VWLT_ATTRIBUTE(optimize("-O3"), target("avx2"))
#include "templates/conv_ci16_4cf32_avx2.t"
DECLARE_TR_FUNC_1_4(conv_ci16_4cf32_avx2)
#endif

conv_function_t conv_get_ci16_4cf32_c(generic_opts_t cpu_cap, const char** sfunc)
{
    const char* fname;
    conv_function_t fn;

    SELECT_GENERIC_FN(fn, fname, tr_conv_ci16_4cf32_generic, cpu_cap);
    SELECT_AVX2_FN(fn, fname, tr_conv_ci16_4cf32_avx2, cpu_cap);

    if (sfunc) *sfunc = fname;
    return fn;
}

conv_function_t conv_get_ci16_4cf32()
{
    return conv_get_ci16_4cf32_c(cpu_vcap_get(), NULL);
}
