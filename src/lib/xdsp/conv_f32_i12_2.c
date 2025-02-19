// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include "conv_f32_i12_2.h"
#include "attribute_switch.h"

#define CONV_SCALE (1.0f/32767)

#define TEMPLATE_FUNC_NAME conv_f32_i12_generic
VWLT_ATTRIBUTE(optimize("-O3"))
#include "templates/conv_f32_i12_generic.t"
DECLARE_TR_FUNC_1_1(conv_f32_i12_generic)

#ifdef WVLT_AVX2
#define TEMPLATE_FUNC_NAME conv_f32_i12_avx2
VWLT_ATTRIBUTE(optimize("-O3"), target("avx2"))
#include "templates/conv_f32_i12_avx2.t"
DECLARE_TR_FUNC_1_1(conv_f32_i12_avx2)
#endif

#ifdef WVLT_NEON
#define TEMPLATE_FUNC_NAME conv_f32_i12_neon
VWLT_ATTRIBUTE(optimize("-O3"))
#include "templates/conv_f32_i12_neon.t"
DECLARE_TR_FUNC_1_1(conv_f32_i12_neon)
#endif  //WVLT_NEON

conv_function_t conv_get_f32_i12_c(generic_opts_t cpu_cap, const char** sfunc)
{
    const char* fname;
    conv_function_t fn;

    SELECT_GENERIC_FN(fn, fname, tr_conv_f32_i12_generic, cpu_cap);
    SELECT_AVX2_FN(fn, fname, tr_conv_f32_i12_avx2, cpu_cap);
    SELECT_NEON_FN(fn, fname, tr_conv_f32_i12_neon, cpu_cap);

    if (sfunc) *sfunc = fname;
    return fn;
}

conv_function_t conv_get_f32_i12()
{
    return conv_get_f32_i12_c(cpu_vcap_get(), NULL);
}
