// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include "conv_filter.h"
#include "attribute_switch.h"


#define TEMPLATE_FUNC_NAME conv_filter_generic
VWLT_ATTRIBUTE(optimize("-O3"))
#include "templates/conv_filter_generic.t"
DECLARE_TR_FUNC_FILTER(conv_filter_generic)

#define TEMPLATE_FUNC_NAME conv_filter_interleave_generic
VWLT_ATTRIBUTE(optimize("-O3"))
#include "templates/conv_filter_interleave_generic.t"
DECLARE_TR_FUNC_FILTER(conv_filter_interleave_generic)

#define TEMPLATE_FUNC_NAME conv_filter_interpolate_generic
VWLT_ATTRIBUTE(optimize("-O3"))
#include "templates/conv_filter_interpolate_generic.t"
DECLARE_TR_FUNC_FILTER(conv_filter_interpolate_generic)

#define TEMPLATE_FUNC_NAME conv_filter_interpolate_interleave_generic
VWLT_ATTRIBUTE(optimize("-O3"))
#include "templates/conv_filter_interpolate_interleave_generic.t"
DECLARE_TR_FUNC_FILTER(conv_filter_interpolate_interleave_generic)


#ifdef __SSSE3__
#define TEMPLATE_FUNC_NAME conv_filter_sse3
VWLT_ATTRIBUTE(optimize("-O3"), target("ssse3"))
#include "templates/conv_filter_sse3.t"
DECLARE_TR_FUNC_FILTER(conv_filter_sse3)

#define TEMPLATE_FUNC_NAME conv_filter_interleave_sse3
VWLT_ATTRIBUTE(optimize("-O3"), target("ssse3"))
#include "templates/conv_filter_interleave_sse3.t"
DECLARE_TR_FUNC_FILTER(conv_filter_interleave_sse3)
#endif //__SSSE3__


#ifdef __AVX2__
#define TEMPLATE_FUNC_NAME conv_filter_avx2
VWLT_ATTRIBUTE(optimize("-O3"), target("avx2"))
#include "templates/conv_filter_avx2.t"
DECLARE_TR_FUNC_FILTER(conv_filter_avx2)

#define TEMPLATE_FUNC_NAME conv_filter_interleave_avx2
VWLT_ATTRIBUTE(optimize("-O3"), target("avx2"))
#include "templates/conv_filter_interleave_avx2.t"
DECLARE_TR_FUNC_FILTER(conv_filter_interleave_avx2)
#endif //__AVX2__


filter_function_t conv_filter_c(generic_opts_t cpu_cap, const char** sfunc)
{
    const char* fname;
    filter_function_t fn;

    SELECT_GENERIC_FN(fn, fname, tr_conv_filter_generic, cpu_cap);
    SELECT_SSSE3_FN(fn, fname, tr_conv_filter_sse3, cpu_cap);
    SELECT_AVX2_FN(fn, fname, tr_conv_filter_avx2, cpu_cap);

    if (sfunc) *sfunc = fname;
    return fn;
}

filter_function_t conv_filter_interleave_c(generic_opts_t cpu_cap, const char **sfunc)
{
    const char* fname;
    filter_function_t fn;

    SELECT_GENERIC_FN(fn, fname, tr_conv_filter_interleave_generic, cpu_cap);
    SELECT_SSSE3_FN(fn, fname, tr_conv_filter_interleave_sse3, cpu_cap);
    SELECT_AVX2_FN(fn, fname, tr_conv_filter_interleave_avx2, cpu_cap);

    if (sfunc) *sfunc = fname;
    return fn;
}

filter_function_t conv_filter_interpolate_c(generic_opts_t cpu_cap, const char **sfunc)
{
    const char* fname;
    filter_function_t fn;

    SELECT_GENERIC_FN(fn, fname, tr_conv_filter_interpolate_generic, cpu_cap);

    if (sfunc) *sfunc = fname;
    return fn;
}

filter_function_t conv_filter_interpolate_interleave_c(generic_opts_t cpu_cap, const char **sfunc)
{
    const char* fname;
    filter_function_t fn;

    SELECT_GENERIC_FN(fn, fname, tr_conv_filter_interpolate_interleave_generic, cpu_cap);

    if (sfunc) *sfunc = fname;
    return fn;
}
