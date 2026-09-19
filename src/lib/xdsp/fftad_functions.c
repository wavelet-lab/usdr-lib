// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include <math.h>
#include "fftad_functions.h"
#include "attribute_switch.h"
#include "fast_math.h"

#define USE_POLYLOG2

#define TEMPLATE_FUNC_NAME fftad_init_generic
VWLT_ATTRIBUTE(optimize("-O3"))
#include "templates/fftad_init_generic.t"
DECLARE_TR_FUNC_FFTAD_INIT(fftad_init_generic)

#define TEMPLATE_FUNC_NAME fftad_add_generic
VWLT_ATTRIBUTE(optimize("-O3"))
#include "templates/fftad_add_generic.t"
DECLARE_TR_FUNC_FFTAD_ADD(fftad_add_generic)

#define TEMPLATE_FUNC_NAME fftad_norm_generic
VWLT_ATTRIBUTE(optimize("-O3"))
#include "templates/fftad_norm_generic.t"
DECLARE_TR_FUNC_FFTAD_NORM(fftad_norm_generic)

#define TEMPLATE_FUNC_NAME fftad_init_hwi16_generic
VWLT_ATTRIBUTE(optimize("-O3"))
#include "templates/fftad_init_hwi16_generic.t"
DECLARE_TR_FUNC_FFTAD_INIT_HWI16(fftad_init_hwi16_generic)

#define TEMPLATE_FUNC_NAME fftad_add_hwi16_generic
VWLT_ATTRIBUTE(optimize("-O3"))
#include "templates/fftad_add_hwi16_generic.t"
DECLARE_TR_FUNC_FFTAD_ADD_HWI16(fftad_add_hwi16_generic)

#define TEMPLATE_FUNC_NAME fftad_norm_hwi16_generic
VWLT_ATTRIBUTE(optimize("-O3"))
#include "templates/fftad_norm_hwi16_generic.t"
DECLARE_TR_FUNC_FFTAD_NORM_HWI16(fftad_norm_hwi16_generic)


#ifdef WVLT_AVX512BW

#define TEMPLATE_FUNC_NAME fftad_init_avx512bw
VWLT_ATTRIBUTE(optimize("-O3"), target("avx512bw"))
#include "templates/fftad_init_avx512bw.t"
DECLARE_TR_FUNC_FFTAD_INIT(fftad_init_avx512bw)

#define TEMPLATE_FUNC_NAME fftad_add_avx512bw
VWLT_ATTRIBUTE(optimize("-O3"), target("avx512bw,avx512dq"))
#include "templates/fftad_add_avx512bw.t"
DECLARE_TR_FUNC_FFTAD_ADD(fftad_add_avx512bw)

#define TEMPLATE_FUNC_NAME fftad_norm_avx512bw
VWLT_ATTRIBUTE(optimize("-O3"), target("avx512bw,avx512dq,fma"))
#include "templates/fftad_norm_avx512bw.t"
DECLARE_TR_FUNC_FFTAD_NORM(fftad_norm_avx512bw)

#endif //WVLT_AVX512BW


#ifdef WVLT_AVX2

#define TEMPLATE_FUNC_NAME fftad_init_avx2
VWLT_ATTRIBUTE(optimize("-O3"), target("avx2"))
#include "templates/fftad_init_avx2.t"
DECLARE_TR_FUNC_FFTAD_INIT(fftad_init_avx2)

#define TEMPLATE_FUNC_NAME fftad_add_avx2
VWLT_ATTRIBUTE(optimize("-O3"), target("avx2"))
#include "templates/fftad_add_avx2.t"
DECLARE_TR_FUNC_FFTAD_ADD(fftad_add_avx2)

#define TEMPLATE_FUNC_NAME fftad_norm_avx2
VWLT_ATTRIBUTE(optimize("-O3"), target("avx2,fma"))
#include "templates/fftad_norm_avx2.t"
DECLARE_TR_FUNC_FFTAD_NORM(fftad_norm_avx2)

#define TEMPLATE_FUNC_NAME fftad_init_hwi16_avx2
VWLT_ATTRIBUTE(optimize("-O3"), target("avx2"))
#include "templates/fftad_init_hwi16_avx2.t"
DECLARE_TR_FUNC_FFTAD_INIT_HWI16(fftad_init_hwi16_avx2)

#define TEMPLATE_FUNC_NAME fftad_add_hwi16_avx2
VWLT_ATTRIBUTE(optimize("-O3"), target("avx2"))
#include "templates/fftad_add_hwi16_avx2.t"
DECLARE_TR_FUNC_FFTAD_ADD_HWI16(fftad_add_hwi16_avx2)

#define TEMPLATE_FUNC_NAME fftad_norm_hwi16_avx2
VWLT_ATTRIBUTE(optimize("-O3"), target("avx2,fma"))
#include "templates/fftad_norm_hwi16_avx2.t"
DECLARE_TR_FUNC_FFTAD_NORM_HWI16(fftad_norm_hwi16_avx2)

#endif //WVLT_AVX2

#ifdef WVLT_NEON

#define TEMPLATE_FUNC_NAME fftad_init_neon
VWLT_ATTRIBUTE(optimize("-O3"))
#include "templates/fftad_init_neon.t"
DECLARE_TR_FUNC_FFTAD_INIT(fftad_init_neon)

#define TEMPLATE_FUNC_NAME fftad_add_neon
VWLT_ATTRIBUTE(optimize("-O3"))
#include "templates/fftad_add_neon.t"
DECLARE_TR_FUNC_FFTAD_ADD(fftad_add_neon)

#define TEMPLATE_FUNC_NAME fftad_norm_neon
VWLT_ATTRIBUTE(optimize("-O3"))
#include "templates/fftad_norm_neon.t"
DECLARE_TR_FUNC_FFTAD_NORM(fftad_norm_neon)

#define TEMPLATE_FUNC_NAME fftad_init_hwi16_neon
VWLT_ATTRIBUTE(optimize("-O3"))
#include "templates/fftad_init_hwi16_neon.t"
DECLARE_TR_FUNC_FFTAD_INIT_HWI16(fftad_init_hwi16_neon)

#define TEMPLATE_FUNC_NAME fftad_add_hwi16_neon
VWLT_ATTRIBUTE(optimize("-O3"))
#include "templates/fftad_add_hwi16_neon.t"
DECLARE_TR_FUNC_FFTAD_ADD_HWI16(fftad_add_hwi16_neon)

#define TEMPLATE_FUNC_NAME fftad_norm_hwi16_neon
VWLT_ATTRIBUTE(optimize("-O3"))
#include "templates/fftad_norm_hwi16_neon.t"
DECLARE_TR_FUNC_FFTAD_NORM_HWI16(fftad_norm_hwi16_neon)

#endif

// Bin(0) = SUM(x0 .. xn) = (A/2) * N
// valueDBFS = 20*log10(abs(value))

fftad_init_function_t fftad_init_c(generic_opts_t cpu_cap, const char** sfunc)
{
    const char* fname;
    fftad_init_function_t fn;

    SELECT_GENERIC_FN(fn, fname, tr_fftad_init_generic, cpu_cap);
    SELECT_AVX2_FN(fn, fname, tr_fftad_init_avx2, cpu_cap);
    SELECT_AVX512BW_FN(fn, fname, tr_fftad_init_avx512bw, cpu_cap);
    SELECT_NEON_FN(fn, fname, tr_fftad_init_neon, cpu_cap);

    if (sfunc) *sfunc = fname;
    return fn;
}

fftad_add_function_t fftad_add_c(generic_opts_t cpu_cap, const char** sfunc)
{
    const char* fname;
    fftad_add_function_t fn;

    SELECT_GENERIC_FN(fn, fname, tr_fftad_add_generic, cpu_cap);
    SELECT_AVX2_FN(fn, fname, tr_fftad_add_avx2, cpu_cap);
    SELECT_AVX512BW_FN(fn, fname, tr_fftad_add_avx512bw, cpu_cap);
    SELECT_NEON_FN(fn, fname, tr_fftad_add_neon, cpu_cap);

    if (sfunc) *sfunc = fname;
    return fn;
}

fftad_norm_function_t fftad_norm_c(generic_opts_t cpu_cap, const char** sfunc)
{
    const char* fname;
    fftad_norm_function_t fn;

    SELECT_GENERIC_FN(fn, fname, tr_fftad_norm_generic, cpu_cap);
    SELECT_AVX2_FN(fn, fname, tr_fftad_norm_avx2, cpu_cap);
    SELECT_AVX512BW_FN(fn, fname, tr_fftad_norm_avx512bw, cpu_cap);
    SELECT_NEON_FN(fn, fname, tr_fftad_norm_neon, cpu_cap);

    if (sfunc) *sfunc = fname;
    return fn;
}


fftad_init_hwi16_function_t fftad_init_hwi16_c(generic_opts_t cpu_cap, const char** sfunc)
{
    const char* fname;
    fftad_init_hwi16_function_t fn;

    SELECT_GENERIC_FN(fn, fname, tr_fftad_init_hwi16_generic, cpu_cap);
    SELECT_AVX2_FN(fn, fname, tr_fftad_init_hwi16_avx2, cpu_cap);
    SELECT_NEON_FN(fn, fname, tr_fftad_init_hwi16_neon, cpu_cap);

    if (sfunc) *sfunc = fname;
    return fn;
}

fftad_add_hwi16_function_t fftad_add_hwi16_c(generic_opts_t cpu_cap, const char** sfunc)
{
    const char* fname;
    fftad_add_hwi16_function_t fn;

    SELECT_GENERIC_FN(fn, fname, tr_fftad_add_hwi16_generic, cpu_cap);
    SELECT_AVX2_FN(fn, fname, tr_fftad_add_hwi16_avx2, cpu_cap);
    SELECT_NEON_FN(fn, fname, tr_fftad_add_hwi16_neon, cpu_cap);

    if (sfunc) *sfunc = fname;
    return fn;
}

fftad_norm_hwi16_function_t fftad_norm_hwi16_c(generic_opts_t cpu_cap, const char** sfunc)
{
    const char* fname;
    fftad_norm_hwi16_function_t fn;

    SELECT_GENERIC_FN(fn, fname, tr_fftad_norm_hwi16_generic, cpu_cap);
    SELECT_AVX2_FN(fn, fname, tr_fftad_norm_hwi16_avx2, cpu_cap);
    SELECT_NEON_FN(fn, fname, tr_fftad_norm_hwi16_neon, cpu_cap);

    if (sfunc) *sfunc = fname;
    return fn;
}
