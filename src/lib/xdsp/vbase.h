// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef VBASE_H
#define VBASE_H

#include <stddef.h>

enum generic_opts {
    OPT_GENERIC = 0,

    // X86-specific
    OPT_SSE = 1000,
    OPT_SSE2,
    OPT_SSE3,
    OPT_SSSE3,
    OPT_SSE41,
    OPT_SSE42,
    OPT_AVX,
    OPT_AVX2,
    OPT_AVX512BW,
    OPT_AVX512VBMI,

    //ARM-specific
    OPT_NEON = 2000,
};
typedef enum generic_opts generic_opts_t;

#define VB_STRINGIFY2(x) #x
#define VB_STRINGIFY(x) VB_STRINGIFY2(x)

#if defined (WVLT_ARCH_X86_64) || defined (WVLT_ARCH_X86)
#define WVLT_SIMD_INTEL
#elif defined (WVLT_ARCH_ARM64)
#define WVLT_SIMD_ARM
#endif


#ifdef WVLT_SIMD_INTEL

#include <immintrin.h>

#ifndef __EMSCRIPTEN__
#define WVLT_AVX512VBMI
#define WVLT_AVX512BW
#define WVLT_AVX2
#define WVLT_AVX
#define WVLT_SSE4_2
#endif  //__EMSCRIPTEN__

#define WVLT_SSE4_1
#define WVLT_SSSE3
#define WVLT_SSE3
#define WVLT_SSE2
#define WVLT_SSE

#elif defined(WVLT_SIMD_ARM)

#include <arm_neon.h>

#define WVLT_NEON

#endif  //WVLT_SIMD_INTEL

#ifdef WVLT_AVX512VBMI
#define SELECT_AVX512VBMI_FN(a, b, fn, cap) do { \
if (cap >= OPT_AVX512VBMI) {a = &fn; b = VB_STRINGIFY(fn);} } while(0)
#else
#define SELECT_AVX512VBMI_FN(a, b, fn, cap)
#endif

#ifdef WVLT_AVX512BW
#define SELECT_AVX512BW_FN(a, b, fn, cap) do { \
    if (cap >= OPT_AVX512BW) {a = &fn; b = VB_STRINGIFY(fn);} } while(0)
#else
#define SELECT_AVX512BW_FN(a, b, fn, cap)
#endif

#ifdef WVLT_AVX2
#define SELECT_AVX2_FN(a, b, fn, cap) do { \
    if (cap >= OPT_AVX2) {a = &fn; b = VB_STRINGIFY(fn);} } while(0)
#else
#define SELECT_AVX2_FN(a, b, fn, cap)
#endif

#ifdef WVLT_AVX
#define SELECT_AVX_FN(a, b, fn, cap) do { \
    if (cap >= OPT_AVX) {a = &fn; b = VB_STRINGIFY(fn);} } while(0)
#else
#define SELECT_AVX_FN(a, b, fn, cap)
#endif

#ifdef WVLT_SSE4_2
#define SELECT_SSE4_2_FN(a, b, fn, cap) do { \
    if (cap >= OPT_SSE42) {a = &fn; b = VB_STRINGIFY(fn);} } while(0)
#else
#define SELECT_SSE4_2_FN(a, b, fn, cap)
#endif

#ifdef WVLT_SSE4_1
#define SELECT_SSE4_1_FN(a, b, fn, cap) do { \
    if (cap >= OPT_SSE41) {a = &fn; b = VB_STRINGIFY(fn);} } while(0)
#else
#define SELECT_SSE4_1_FN(a, b, fn, cap)
#endif

#ifdef WVLT_SSSE3
#define SELECT_SSSE3_FN(a, b, fn, cap) do { \
    if (cap >= OPT_SSSE3) {a = &fn; b = VB_STRINGIFY(fn);} } while(0)
#else
#define SELECT_SSSE3_FN(a, b, fn, cap)
#endif

#ifdef WVLT_SSE3
#define SELECT_SSE3_FN(a, b, fn, cap) do { \
    if (cap >= OPT_SSE3) {a = &fn; b = VB_STRINGIFY(fn);} } while(0)
#else
#define SELECT_SSE3_FN(a, b, fn, cap)
#endif

#ifdef WVLT_SSE2
#define SELECT_SSE2_FN(a, b, fn, cap) do { \
    if (cap >= OPT_SSE2) {a = &fn; b = VB_STRINGIFY(fn);} } while(0)
#else
#define SELECT_SSE2_FN(a, b, fn, cap)
#endif

#ifdef WVLT_SSE
#define SELECT_SSE_FN(a, b, fn, cap) do { \
    if (cap >= OPT_SSE) {a = &fn; b = VB_STRINGIFY(fn);} } while(0)
#else
#define SELECT_SSE_FN(a, b, fn, cap)
#endif

#ifdef WVLT_NEON
#define SELECT_NEON_FN(a, b, fn, cap) do { \
    if (cap >= OPT_NEON) {a = &fn; b = VB_STRINGIFY(fn);} } while(0)
#else
#define SELECT_NEON_FN(a, b, fn, cap)
#endif


#define SELECT_GENERIC_FN(a, b, fn, cap) do { \
a = &fn; b = VB_STRINGIFY(fn); } while(0)

enum {
    CVF_LIMIT_VCPU = 0x80000000,
};

#ifdef __cplusplus
extern "C" {
#endif

generic_opts_t cpu_vcap_obtain(unsigned flags);
void cpu_vcap_str(char* bufer, unsigned buflen, generic_opts_t caps);
generic_opts_t cpu_vcap_get(void);
unsigned cpu_vcap_align(generic_opts_t caps);

#ifdef __cplusplus
}
#endif

#endif
