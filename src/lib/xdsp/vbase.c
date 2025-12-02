// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include "vbase.h"
#include <string.h>

static generic_opts_t g_cpu_vcap = OPT_GENERIC;

generic_opts_t cpu_vcap_obtain(unsigned flags)
{
    generic_opts_t cap = OPT_GENERIC;

#ifdef WVLT_SIMD_ARM
    cap = OPT_NEON; //aarch64 should _always_ support NEON

#elif defined(WVLT_SIMD_INTEL)

#ifdef __EMSCRIPTEN__
    cap = OPT_SSE41;

#else
    unsigned max_cpu = OPT_AVX512VBMI;

    if (flags & CVF_LIMIT_VCPU) {
        max_cpu = (flags & 0xffff);
    }

    __builtin_cpu_init();
    if (__builtin_cpu_supports("avx512vbmi") && max_cpu >= OPT_AVX512VBMI)
        cap = OPT_AVX512VBMI;
    else if (__builtin_cpu_supports("avx512bw") && max_cpu >= OPT_AVX512BW)
        cap = OPT_AVX512BW;
    else if (__builtin_cpu_supports("avx2") && max_cpu >= OPT_AVX2)
        cap = OPT_AVX2;
    else if (__builtin_cpu_supports("avx") && max_cpu >= OPT_AVX)
        cap = OPT_AVX;
    else if (__builtin_cpu_supports("sse4.2") && max_cpu >= OPT_SSE42)
        cap = OPT_SSE42;
    else if (__builtin_cpu_supports("sse4.1") && max_cpu >= OPT_SSE41)
        cap = OPT_SSE41;
    else if (__builtin_cpu_supports("ssse3") && max_cpu >= OPT_SSSE3)
        cap = OPT_SSSE3;
    else if (__builtin_cpu_supports("sse3") && max_cpu >= OPT_SSE3)
        cap = OPT_SSE3;
    else if (__builtin_cpu_supports("sse2") && max_cpu >= OPT_SSE2)
        cap = OPT_SSE2;
    else if (__builtin_cpu_supports("sse") && max_cpu >= OPT_SSE)
        cap = OPT_SSE;

#endif  //__EMSCRIPTEN__
#endif  //WVLT_SIMD_ARM

    g_cpu_vcap = cap;
    return cap;
}

void cpu_vcap_str(char* buffer, unsigned buflen, generic_opts_t caps)
{
    const char* type = "[generic]";

    switch (caps) {
    case OPT_GENERIC: break;
    case OPT_SSE: type = "SSE"; break;
    case OPT_SSE2: type = "SSE2"; break;
    case OPT_SSE3: type = "SSE3"; break;
    case OPT_SSSE3: type = "SSSE3"; break;
    case OPT_SSE41: type = "SSE4.1"; break;
    case OPT_SSE42: type = "SSE4.2"; break;
    case OPT_AVX: type = "AVX"; break;
    case OPT_AVX2: type = "AVX2"; break;
    case OPT_AVX512BW: type = "AVX512BW"; break;
    case OPT_AVX512VBMI: type = "AVX512VBMI"; break;

    case OPT_NEON: type = "ARM_NEON"; break;
    }

    strncpy(buffer, type, buflen);
}

unsigned cpu_vcap_align(generic_opts_t caps)
{
    switch (caps) {
    case OPT_SSE:
    case OPT_SSE2:
    case OPT_SSE3:
    case OPT_SSSE3:
    case OPT_SSE41:
    case OPT_SSE42:     return 16;
    case OPT_AVX:
    case OPT_AVX2:      return 32;
    case OPT_AVX512VBMI:
    case OPT_AVX512BW:  return 64;
    case OPT_NEON:      return 16;
    default:
        return 8;
    }
}


generic_opts_t cpu_vcap_get(void)
{
    return g_cpu_vcap;
}
