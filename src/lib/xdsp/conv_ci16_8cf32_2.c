// Copyright (c) 2025 Wavelet Lab
// SPDX-License-Identifier: MIT

#include "conv_ci16_8cf32_2.h"
#include "attribute_switch.h"

#define CONV_SCALE (1.0f/32767)

#define TEMPLATE_FUNC_NAME conv_ci16_8cf32_generic
VWLT_ATTRIBUTE(optimize("-O3"))
#include "templates/conv_ci16_8cf32_generic.t"
DECLARE_TR_FUNC_1_8(conv_ci16_8cf32_generic)


conv_function_t conv_get_ci16_8cf32_c(generic_opts_t cpu_cap, const char** sfunc)
{
    const char* fname;
    conv_function_t fn;

    SELECT_GENERIC_FN(fn, fname, tr_conv_ci16_8cf32_generic, cpu_cap);

    if (sfunc) *sfunc = fname;
    return fn;
}

conv_function_t conv_get_ci16_8cf32()
{
    return conv_get_ci16_8cf32_c(cpu_vcap_get(), NULL);
}
