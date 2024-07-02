// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef FAST_MATH_H
#define FAST_MATH_H

#include <stdint.h>
#include "vbase.h"

#define WVLT_FASTLOG2_MUL    1.1920928955078125E-7f
#define WVLT_FASTLOG2_SUB    126.94269504f

/*
 * Log2 Mitchell’s Approximation, eps about 0.05f
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
 * eps about 0.00005f
 * https://jrfonseca.blogspot.com/2008/09/fast-sse2-pow-tables-or-polynomials.html
*/
#ifdef WVLT_AVX2

#define WVLT_LOG2_POLY0(x, c0) _mm256_set1_ps(c0)
#define WVLT_LOG2_POLY1(x, c0, c1) _mm256_add_ps(_mm256_mul_ps(WVLT_LOG2_POLY0(x, c1), x), _mm256_set1_ps(c0))
#define WVLT_LOG2_POLY2(x, c0, c1, c2) _mm256_add_ps(_mm256_mul_ps(WVLT_LOG2_POLY1(x, c1, c2), x), _mm256_set1_ps(c0))
#define WVLT_LOG2_POLY3(x, c0, c1, c2, c3) _mm256_add_ps(_mm256_mul_ps(WVLT_LOG2_POLY2(x, c1, c2, c3), x), _mm256_set1_ps(c0))
#define WVLT_LOG2_POLY4(x, c0, c1, c2, c3, c4) _mm256_add_ps(_mm256_mul_ps(WVLT_LOG2_POLY3(x, c1, c2, c3, c4), x), _mm256_set1_ps(c0))
#define WVLT_LOG2_POLY5(x, c0, c1, c2, c3, c4, c5) _mm256_add_ps(_mm256_mul_ps(WVLT_LOG2_POLY4(x, c1, c2, c3, c4, c5), x), _mm256_set1_ps(c0))

#define WVLT_LOG2_DECL_CONSTS \
    const __m256i wvlt_log2_exp  = _mm256_set1_epi32(0x7F800000); \
    const __m256i wvlt_log2_mant = _mm256_set1_epi32(0x007FFFFF); \
    const __m256  wvlt_log2_one  = _mm256_set1_ps( 1.0f); \
    const __m256i wvlt_log2_v127 = _mm256_set1_epi32(127);

#define WVLT_LOG2F8(in, out) \
{ \
    __m256i i = _mm256_castps_si256(in); \
    __m256  e = _mm256_cvtepi32_ps(_mm256_sub_epi32(_mm256_srli_epi32(_mm256_and_si256(i, wvlt_log2_exp), 23), wvlt_log2_v127)); \
    __m256  m = _mm256_or_ps(_mm256_castsi256_ps(_mm256_and_si256(i, wvlt_log2_mant)), wvlt_log2_one); \
  \
   /* Minimax polynomial fit of log2(x)/(x - 1), for x in range [1, 2[ */ \
   __m256 p = WVLT_LOG2_POLY2(m, 2.28330284476918490682f, -1.04913055217340124191f, 0.204446009836232697516f); \
  \
    /* This effectively increases the polynomial degree by one, but ensures that log2(1) == 0*/ \
    p = _mm256_mul_ps(p, _mm256_sub_ps(m, wvlt_log2_one)); \
  \
    out = _mm256_add_ps(p, e); \
}

/*
 *   Variants (fast <-> accurate):
 *
 *    __m256 p = WVLT_LOG2_POLY2(m, 2.28330284476918490682f, -1.04913055217340124191f, 0.204446009836232697516f);
 *    __m256 p = WVLT_LOG2_POLY3(m, 2.61761038894603480148f, -1.75647175389045657003f, 0.688243882994381274313f, -0.107254423828329604454f);
 *    __m256 p = WVLT_LOG2_POLY4(m, 2.8882704548164776201f, -2.52074962577807006663f, 1.48116647521213171641f, -0.465725644288844778798f, 0.0596515482674574969533f);
 *    __m256 p = WVLT_LOG2_POLY5(m, 3.1157899f, -3.3241990f, 2.5988452f, -1.2315303f,  3.1821337e-1f, -3.4436006e-2f);
*/

#endif

#endif // FAST_MATH_H
