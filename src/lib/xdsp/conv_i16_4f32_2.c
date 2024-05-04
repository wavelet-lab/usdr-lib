// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include "conv_i16_4f32_2.h"
#include "attribute_switch.h"

#define CONV_SCALE (1.0f/32767)

#define TEMPLATE_FUNC_NAME conv_i16_4f32_generic
VWLT_ATTRIBUTE(optimize("-O3"))
#include "templates/conv_i16_4f32_generic.t"
DECLARE_TR_FUNC_1_4(conv_i16_4f32_generic)



conv_function_t conv_get_i16_4f32_c(generic_opts_t cpu_cap, const char** sfunc)
{
    const char* fname;
    conv_function_t fn;

    SELECT_GENERIC_FN(fn, fname, tr_conv_i16_4f32_generic, cpu_cap);
    //SELECT_SSE2_FN(fn, fname, tr_conv_ci16_2cf32_generic, cpu_cap);
    //SELECT_AVX_FN(fn, fname, tr_conv_ci16_2cf32_generic, cpu_cap);

    if (sfunc) *sfunc = fname;
    return fn;
}

conv_function_t conv_get_i16_4f32()
{
    return conv_get_i16_4f32_c(cpu_vcap_get(), NULL);
}
