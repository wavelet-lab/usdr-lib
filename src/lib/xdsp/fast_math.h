// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef FAST_MATH_H
#define FAST_MATH_H

#include <stdint.h>
#include "vbase.h"

#define WVLT_FASTLOG2_MUL    1.1920928955078125E-7f
#define WVLT_FASTLOG2_SUB    126.94269504f

typedef float (*wvlt_log2f_fn_t)(float);

/*
 * Log2 Mitchell’s Approximation, max eps about 0.05f
 */
static inline
float wvlt_fastlog2(float x)
{
    union {float f32; uint32_t u32;} u_fi = { x };
    float y = u_fi.u32;
    y *= WVLT_FASTLOG2_MUL;
    y -= WVLT_FASTLOG2_SUB;
    return y;
}

/*
 * José Fonseca (c)
 * https://jrfonseca.blogspot.com/2008/09/fast-sse2-pow-tables-or-polynomials.html
*/

#define LOG_POLY_DEGREE 3

static inline
    float wvlt_polylog2f(float x)
{
    static const int32_t exp  = 0x7F800000;
    static const int32_t mant = 0x007FFFFF;
    const static union {float f32; int32_t i32;} f_one = { 1.0f };

    union {float f32; int32_t i32;} i = { x };
    float e = (float)(((i.i32 & exp) >> 23) - 127);
    union {int32_t i32; float f32;} m = { (i.i32 & mant) | f_one.i32 };

#if   LOG_POLY_DEGREE == 3
    float p = m.f32 * (m.f32 * 0.204446009836232697516f - 1.04913055217340124191f) + 2.28330284476918490682f;
#elif LOG_POLY_DEGREE == 4
    float p = m.f32 * (m.f32 * (m.f32 * (-0.107254423828329604454f) + 0.688243882994381274313f) - 1.75647175389045657003f) + 2.61761038894603480148f;
#elif LOG_POLY_DEGREE == 5
    float p = m.f32 * (m.f32 * (m.f32 * (m.f32 * 0.0596515482674574969533f - 0.465725644288844778798f) + 1.48116647521213171641f) - 2.52074962577807006663f) + 2.8882704548164776201f;
#elif LOG_POLY_DEGREE == 6
    float p = m.f32 * (m.f32 * (m.f32 * (m.f32 * (m.f32 * (-3.4436006e-2f) + 3.1821337e-1f) - 1.2315303f) + 2.5988452f) - 3.3241990f) + 3.1157899f;
#else
#error
#endif

    p *= (m.f32 - f_one.f32);
    return p + e;
}

#ifdef WVLT_AVX2

#define WVLT_LOG2_POLY0(x, c0) _mm256_set1_ps(c0)
#define WVLT_LOG2_POLY1(x, c0, c1) _mm256_add_ps(_mm256_mul_ps(WVLT_LOG2_POLY0(x, c1), x), _mm256_set1_ps(c0))
#define WVLT_LOG2_POLY2(x, c0, c1, c2) _mm256_add_ps(_mm256_mul_ps(WVLT_LOG2_POLY1(x, c1, c2), x), _mm256_set1_ps(c0))
#define WVLT_LOG2_POLY3(x, c0, c1, c2, c3) _mm256_add_ps(_mm256_mul_ps(WVLT_LOG2_POLY2(x, c1, c2, c3), x), _mm256_set1_ps(c0))
#define WVLT_LOG2_POLY4(x, c0, c1, c2, c3, c4) _mm256_add_ps(_mm256_mul_ps(WVLT_LOG2_POLY3(x, c1, c2, c3, c4), x), _mm256_set1_ps(c0))
#define WVLT_LOG2_POLY5(x, c0, c1, c2, c3, c4, c5) _mm256_add_ps(_mm256_mul_ps(WVLT_LOG2_POLY4(x, c1, c2, c3, c4, c5), x), _mm256_set1_ps(c0))

#define WVLT_POLYLOG2_DECL_CONSTS \
    const __m256i wvlt_log2_exp  = _mm256_set1_epi32(0x7F800000); \
    const __m256i wvlt_log2_mant = _mm256_set1_epi32(0x007FFFFF); \
    const __m256  wvlt_log2_one  = _mm256_set1_ps(1.0f); \
    const __m256i wvlt_log2_v127 = _mm256_set1_epi32(127);

#if   LOG_POLY_DEGREE == 3
#define WVLT_LOG2_POLY_APPROX(x) WVLT_LOG2_POLY2(x, 2.28330284476918490682f, -1.04913055217340124191f, 0.204446009836232697516f)
#elif LOG_POLY_DEGREE == 4
#define WVLT_LOG2_POLY_APPROX(x) WVLT_LOG2_POLY3(x, 2.61761038894603480148f, -1.75647175389045657003f, 0.688243882994381274313f, -0.107254423828329604454f)
#elif LOG_POLY_DEGREE == 5
#define WVLT_LOG2_POLY_APPROX(x) WVLT_LOG2_POLY4(x, 2.8882704548164776201f, -2.52074962577807006663f, 1.48116647521213171641f, -0.465725644288844778798f, 0.0596515482674574969533f)
#elif LOG_POLY_DEGREE == 6
#define WVLT_LOG2_POLY_APPROX(x) WVLT_LOG2_POLY5(x, 3.1157899f, -3.3241990f, 2.5988452f, -1.2315303f,  3.1821337e-1f, -3.4436006e-2f)
#else
#error
#endif

#define WVLT_POLYLOG2F8(in, out) \
{ \
    __m256i i = _mm256_castps_si256(in); \
    __m256  e = _mm256_cvtepi32_ps(_mm256_sub_epi32(_mm256_srli_epi32(_mm256_and_si256(i, wvlt_log2_exp), 23), wvlt_log2_v127)); \
    __m256  m = _mm256_or_ps(_mm256_castsi256_ps(_mm256_and_si256(i, wvlt_log2_mant)), wvlt_log2_one); \
  \
   /* Minimax polynomial fit of log2(x)/(x - 1), for x in range [1, 2[ */ \
   __m256 p = WVLT_LOG2_POLY_APPROX(m); \
  \
    /* This effectively increases the polynomial degree by one, but ensures that log2(1) == 0*/ \
    p = _mm256_mul_ps(p, _mm256_sub_ps(m, wvlt_log2_one)); \
  \
    out = _mm256_add_ps(p, e); \
}

#endif



#ifdef WVLT_AVX512BW

#define WVLT_AVX512_LOG2_POLY0(x, c0) _mm512_set1_ps(c0)
#define WVLT_AVX512_LOG2_POLY1(x, c0, c1) _mm512_add_ps(_mm512_mul_ps(WVLT_AVX512_LOG2_POLY0(x, c1), x), _mm512_set1_ps(c0))
#define WVLT_AVX512_LOG2_POLY2(x, c0, c1, c2) _mm512_add_ps(_mm512_mul_ps(WVLT_AVX512_LOG2_POLY1(x, c1, c2), x), _mm512_set1_ps(c0))
#define WVLT_AVX512_LOG2_POLY3(x, c0, c1, c2, c3) _mm512_add_ps(_mm512_mul_ps(WVLT_AVX512_LOG2_POLY2(x, c1, c2, c3), x), _mm512_set1_ps(c0))
#define WVLT_AVX512_LOG2_POLY4(x, c0, c1, c2, c3, c4) _mm512_add_ps(_mm512_mul_ps(WVLT_AVX512_LOG2_POLY3(x, c1, c2, c3, c4), x), _mm512_set1_ps(c0))
#define WVLT_AVX512_LOG2_POLY5(x, c0, c1, c2, c3, c4, c5) _mm512_add_ps(_mm512_mul_ps(WVLT_AVX512_LOG2_POLY4(x, c1, c2, c3, c4, c5), x), _mm512_set1_ps(c0))

#define WVLT_AVX512_POLYLOG2_DECL_CONSTS \
const __m512i wvlt_AVX512_log2_exp  = _mm512_set1_epi32(0x7F800000); \
    const __m512i wvlt_AVX512_log2_mant = _mm512_set1_epi32(0x007FFFFF); \
    const __m512  wvlt_AVX512_log2_one  = _mm512_set1_ps(1.0f); \
    const __m512i wvlt_AVX512_log2_v127 = _mm512_set1_epi32(127);

#if   LOG_POLY_DEGREE == 3
#define WVLT_AVX512_LOG2_POLY_APPROX(x) WVLT_AVX512_LOG2_POLY2(x, 2.28330284476918490682f, -1.04913055217340124191f, 0.204446009836232697516f)
#elif LOG_POLY_DEGREE == 4
#define WVLT_AVX512_LOG2_POLY_APPROX(x) WVLT_AVX512_LOG2_POLY3(x, 2.61761038894603480148f, -1.75647175389045657003f, 0.688243882994381274313f, -0.107254423828329604454f)
#elif LOG_POLY_DEGREE == 5
#define WVLT_AVX512_LOG2_POLY_APPROX(x) WVLT_AVX512_LOG2_POLY4(x, 2.8882704548164776201f, -2.52074962577807006663f, 1.48116647521213171641f, -0.465725644288844778798f, 0.0596515482674574969533f)
#elif LOG_POLY_DEGREE == 6
#define WVLT_AVX512_LOG2_POLY_APPROX(x) WVLT_AVX512_LOG2_POLY5(x, 3.1157899f, -3.3241990f, 2.5988452f, -1.2315303f,  3.1821337e-1f, -3.4436006e-2f)
#else
#error
#endif

#define WVLT_POLYLOG2F16(in, out) \
{ \
        __m512i i = _mm512_castps_si512(in); \
        __m512  e = _mm512_cvtepi32_ps(_mm512_sub_epi32(_mm512_srli_epi32(_mm512_and_si512(i, wvlt_AVX512_log2_exp), 23), wvlt_AVX512_log2_v127)); \
        __m512  m = _mm512_or_ps(_mm512_castsi512_ps(_mm512_and_si512(i, wvlt_AVX512_log2_mant)), wvlt_AVX512_log2_one); \
  \
        /* Minimax polynomial fit of log2(x)/(x - 1), for x in range [1, 2[ */ \
        __m512 p = WVLT_AVX512_LOG2_POLY_APPROX(m); \
  \
        /* This effectively increases the polynomial degree by one, but ensures that log2(1) == 0*/ \
        p = _mm512_mul_ps(p, _mm512_sub_ps(m, wvlt_AVX512_log2_one)); \
  \
        out = _mm512_add_ps(p, e); \
}

#endif

#ifdef WVLT_NEON

#define WVLT_LOG2_POLY0(x, c0) vdupq_n_f32(c0)
#define WVLT_LOG2_POLY1(x, c0, c1) vmlaq_f32(vdupq_n_f32(c0), WVLT_LOG2_POLY0(x, c1), x)
#define WVLT_LOG2_POLY2(x, c0, c1, c2) vmlaq_f32(vdupq_n_f32(c0), WVLT_LOG2_POLY1(x, c1, c2), x)
#define WVLT_LOG2_POLY3(x, c0, c1, c2, c3) vmlaq_f32(vdupq_n_f32(c0), WVLT_LOG2_POLY2(x, c1, c2, c3), x)
#define WVLT_LOG2_POLY4(x, c0, c1, c2, c3, c4) vmlaq_f32(vdupq_n_f32(c0), WVLT_LOG2_POLY3(x, c1, c2, c3, c4), x)
#define WVLT_LOG2_POLY5(x, c0, c1, c2, c3, c4, c5) vmlaq_f32(vdupq_n_f32(c0), WVLT_LOG2_POLY4(x, c1, c2, c3, c4, c5), x)

#define WVLT_POLYLOG2_DECL_CONSTS \
    const int32x4_t   wvlt_log2_exp   = vdupq_n_s32(0x7F800000); \
    const int32x4_t   wvlt_log2_mant  = vdupq_n_s32(0x007FFFFF); \
    const float32x4_t wvlt_log2_f_one = vdupq_n_f32(1.0f); \
    const int32x4_t   wvlt_log2_i_one = vreinterpretq_s32_f32(wvlt_log2_f_one); \
    const int32x4_t   wvlt_log2_v127  = vdupq_n_s32(-127);

#if   LOG_POLY_DEGREE == 3
#define WVLT_LOG2_POLY_APPROX(x) WVLT_LOG2_POLY2(x, 2.28330284476918490682f, -1.04913055217340124191f, 0.204446009836232697516f)
#elif LOG_POLY_DEGREE == 4
#define WVLT_LOG2_POLY_APPROX(x) WVLT_LOG2_POLY3(x, 2.61761038894603480148f, -1.75647175389045657003f, 0.688243882994381274313f, -0.107254423828329604454f)
#elif LOG_POLY_DEGREE == 5
#define WVLT_LOG2_POLY_APPROX(x) WVLT_LOG2_POLY4(x, 2.8882704548164776201f, -2.52074962577807006663f, 1.48116647521213171641f, -0.465725644288844778798f, 0.0596515482674574969533f)
#elif LOG_POLY_DEGREE == 6
#define WVLT_LOG2_POLY_APPROX(x) WVLT_LOG2_POLY5(x, 3.1157899f, -3.3241990f, 2.5988452f, -1.2315303f,  3.1821337e-1f, -3.4436006e-2f)
#else
#error
#endif

#define WVLT_POLYLOG2F8(in, out) \
{ \
        int32x4_t   i = vreinterpretq_s32_f32(in); \
        float32x4_t e = vcvtq_f32_s32(vsraq_n_s32(wvlt_log2_v127, vandq_s32(i, wvlt_log2_exp), 23)); \
        float32x4_t m = vreinterpretq_f32_s32(vorrq_s32(vandq_s32(i, wvlt_log2_mant), wvlt_log2_i_one)); \
  \
        /* Minimax polynomial fit of log2(x)/(x - 1), for x in range [1, 2[ */ \
        float32x4_t p = WVLT_LOG2_POLY_APPROX(m); \
  \
        /* This effectively increases the polynomial degree by one, but ensures that log2(1) == 0*/ \
        p = vmulq_f32(p, vsubq_f32(m, wvlt_log2_f_one)); \
  \
        out = vaddq_f32(p, e); \
}

#endif

#define DBFS_TO_AMPLITUDE(dbfs, max_amplitude) ((max_amplitude) * pow(2, (float)(dbfs) / 6.020599913f))
#define AMPLITUDE_TO_DBFS(amplitude, max_amplitude) \
    (fabs(amplitude) < 1E-10 ? -100.0 : 6.020599913f * wvlt_polylog2f(fabs(amplitude) / fabs(max_amplitude)))

#endif // FAST_MATH_H
