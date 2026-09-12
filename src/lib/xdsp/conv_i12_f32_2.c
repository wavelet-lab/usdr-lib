// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include "conv_i12_f32_2.h"
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

#define TEMPLATE_FUNC_NAME conv_i12_f32_generic
VWLT_ATTRIBUTE(optimize("-O3"))
#include "templates/conv_i12_f32_generic.t"
DECLARE_TR_FUNC_1_1(conv_i12_f32_generic)

#ifdef WVLT_AVX512BW
#define TEMPLATE_FUNC_NAME conv_i12_f32_avx512bw
VWLT_ATTRIBUTE(optimize("-O3"), target("avx512bw"))
#include "templates/conv_i12_f32_avx512bw.t"
DECLARE_TR_FUNC_1_1(conv_i12_f32_avx512bw)
#endif

#ifdef WVLT_AVX2
#define TEMPLATE_FUNC_NAME conv_i12_f32_avx2
VWLT_ATTRIBUTE(optimize("-O3"), target("avx2"))
#include "templates/conv_i12_f32_avx2.t"
DECLARE_TR_FUNC_1_1(conv_i12_f32_avx2)
#endif

#ifdef WVLT_NEON
#define TEMPLATE_FUNC_NAME conv_i12_f32_neon
VWLT_ATTRIBUTE(optimize("-O3"))
#include "templates/conv_i12_f32_neon.t"
DECLARE_TR_FUNC_1_1(conv_i12_f32_neon)
#endif

conv_function_t conv_get_i12_f32_c(generic_opts_t cpu_cap, const char** sfunc)
{
    const char* fname;
    conv_function_t fn;

    SELECT_GENERIC_FN(fn, fname, tr_conv_i12_f32_generic, cpu_cap);
    SELECT_AVX2_FN(fn, fname, tr_conv_i12_f32_avx2, cpu_cap);
    SELECT_AVX512BW_FN(fn, fname, tr_conv_i12_f32_avx512bw, cpu_cap);
    SELECT_NEON_FN(fn, fname, tr_conv_i12_f32_neon, cpu_cap);

    if (sfunc) *sfunc = fname;
    return fn;
}

conv_function_t conv_get_i12_f32()
{
    return conv_get_i12_f32_c(cpu_vcap_get(), NULL);
}
