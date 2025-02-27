// Copyright (c) 2025 Wavelet Lab
// SPDX-License-Identifier: MIT

#define _GNU_SOURCE
#include "math.h"

#include "sincos_functions.h"
#include "attribute_switch.h"

#define TEMPLATE_FUNC_NAME wvlt_sincos_i16_generic
VWLT_ATTRIBUTE(optimize("-O3", "inline"))
#include "templates/wvlt_sincos_i16_generic.t"
DECLARE_TR_FUNC_1_2(wvlt_sincos_i16_generic)

#ifdef WVLT_SSSE3
#define TEMPLATE_FUNC_NAME wvlt_sincos_i16_ssse3
VWLT_ATTRIBUTE(optimize("-O3", "inline"), target("ssse3"))
#include "templates/wvlt_sincos_i16_ssse3.t"
DECLARE_TR_FUNC_1_2(wvlt_sincos_i16_ssse3)
#endif

#ifdef WVLT_NEON
#define TEMPLATE_FUNC_NAME wvlt_sincos_i16_neon
VWLT_ATTRIBUTE(optimize("-O3", "inline"))
#include "templates/wvlt_sincos_i16_neon.t"
DECLARE_TR_FUNC_1_2(wvlt_sincos_i16_neon)
#endif

conv_function_t get_wvlt_sincos_i16_c(generic_opts_t cpu_cap, const char** sfunc)
{
    const char* fname;
    conv_function_t fn;

    SELECT_GENERIC_FN(fn, fname, tr_wvlt_sincos_i16_generic, cpu_cap);
    SELECT_SSSE3_FN(fn, fname, tr_wvlt_sincos_i16_ssse3, cpu_cap);
    //SELECT_AVX2_FN(fn, fname, tr_wvlt_sincos_i16_avx2, cpu_cap);
    SELECT_NEON_FN(fn, fname, tr_wvlt_sincos_i16_neon, cpu_cap);

    if (sfunc) *sfunc = fname;
    return fn;
}

conv_function_t get_wvlt_sincos_i16()
{
    return get_wvlt_sincos_i16_c(cpu_vcap_get(), NULL);
}


#define TEMPLATE_FUNC_NAME wvlt_sincos_i16_interleaved_ctrl_generic
VWLT_ATTRIBUTE(optimize("-O3", "inline"))
#include "templates/wvlt_sincos_i16_interleaved_ctrl_generic.t"
DECLARE_TR_FUNC_SINCOS_I16_INTERLEAVED_CTRL(wvlt_sincos_i16_interleaved_ctrl_generic)

#ifdef WVLT_SSSE3
#define TEMPLATE_FUNC_NAME wvlt_sincos_i16_interleaved_ctrl_ssse3
VWLT_ATTRIBUTE(optimize("-O3", "inline"), target("ssse3"))
#include "templates/wvlt_sincos_i16_interleaved_ctrl_ssse3.t"
DECLARE_TR_FUNC_SINCOS_I16_INTERLEAVED_CTRL(wvlt_sincos_i16_interleaved_ctrl_ssse3)
#endif

/*
#ifdef WVLT_NEON
#define TEMPLATE_FUNC_NAME wvlt_sincos_i16_interleaved_ctrl_neon
VWLT_ATTRIBUTE(optimize("-O3", "inline"))
#include "templates/wvlt_sincos_i16_interleaved_ctrl_neon.t"
DECLARE_TR_FUNC_SINCOS_I16_INTERLEAVED_CTRL(wvlt_sincos_i16_interleaved_ctrl_neon)
#endif
*/

sincos_i16_interleaved_ctrl_function_t get_wvlt_sincos_i16_interleaved_ctrl_c(generic_opts_t cpu_cap, const char** sfunc)
{
    const char* fname;
    sincos_i16_interleaved_ctrl_function_t fn;

    SELECT_GENERIC_FN(fn, fname, tr_wvlt_sincos_i16_interleaved_ctrl_generic, cpu_cap);
    SELECT_SSSE3_FN(fn, fname, tr_wvlt_sincos_i16_interleaved_ctrl_ssse3, cpu_cap);
    //SELECT_AVX2_FN(fn, fname, tr_wvlt_sincos_i16_interleaved_ctrl_avx2, cpu_cap);
    //SELECT_NEON_FN(fn, fname, tr_wvlt_sincos_i16_interleaved_ctrl_neon, cpu_cap);

    if (sfunc) *sfunc = fname;
    return fn;
}

sincos_i16_interleaved_ctrl_function_t get_wvlt_sincos_i16_interleaved_ctrl()
{
    return get_wvlt_sincos_i16_interleaved_ctrl_c(cpu_vcap_get(), NULL);
}
