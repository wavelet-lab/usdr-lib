// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef TRIG_INLINE_H
#define TRIG_INLINE_H

#if defined(__x86_64) || defined(__x86)
#include <immintrin.h>


__attribute__((optimize("O3", "inline"), target("ssse3"), unused))
static void isincos_ssse3(__m128i* pph, __m128i* psin, __m128i *pcos)
{
    __m128i ph = _mm_loadu_si128((__m128i*)pph);
#ifdef CORR_32768
    __m128i amsk = _mm_cmpeq_epi16(ph, _mm_set1_epi16(-32768));
#endif
    __m128i ph2 = _mm_mulhrs_epi16(ph, ph);
    __m128i phx1 = _mm_mulhrs_epi16(ph, _mm_set1_epi16(18705));
    __m128i phx3_c = _mm_mulhrs_epi16(ph, _mm_set1_epi16(-21166));
    __m128i phx5_c = _mm_mulhrs_epi16(ph, _mm_set1_epi16(2611));
    __m128i phx7_c = _mm_mulhrs_epi16(ph, _mm_set1_epi16(-152));
    __m128i ph4 = _mm_mulhrs_epi16(ph2, ph2);
    __m128i phx3 = _mm_mulhrs_epi16(ph2, phx3_c);
    __m128i phy2 = _mm_mulhrs_epi16(ph2, _mm_set1_epi16(-7656));
    __m128i phs0 = _mm_add_epi16(ph, phx1);
    __m128i phc0 = _mm_sub_epi16(_mm_set1_epi16(32767), ph2);
    __m128i phs1 = _mm_add_epi16(phs0, phx3);
    __m128i phc1 = _mm_add_epi16(phc0, phy2);
    __m128i ph6 = _mm_mulhrs_epi16(ph4, ph2);
    __m128i phx5 = _mm_mulhrs_epi16(ph4, phx5_c);
    __m128i phy48 = _mm_mulhrs_epi16(ph4, _mm_set1_epi16(30));
    __m128i phy4 = _mm_mulhrs_epi16(ph4, _mm_set1_epi16(8311));
#ifdef CORR_32768
    __m128i ccorr = _mm_and_si128(amsk, _mm_set1_epi16(-57));
    __m128i scorr = _mm_and_si128(amsk, _mm_set1_epi16(-28123));
#else
    // dummy
#endif
    __m128i phy6 = _mm_mulhrs_epi16(ph6, _mm_set1_epi16(-683));
    __m128i phx7 = _mm_mulhrs_epi16(ph6, phx7_c);
    __m128i phy8 = _mm_mulhrs_epi16(ph4, phy48);
    __m128i phs2 = _mm_add_epi16(phs1, phx5);
    __m128i phc2 = _mm_add_epi16(phc1, phy4);
#ifdef CORR_32768
    phs2 = _mm_add_epi16(phs2, scorr);
    phc2 = _mm_add_epi16(phc2, ccorr);
#else
    // dummy
    // dummy
#endif
    __m128i phs3 = _mm_add_epi16(phs2, phx7);
    __m128i phc3 = _mm_add_epi16(phc2, phy6);
    __m128i phc4 = _mm_add_epi16(phc3, phy8);

    _mm_storeu_si128((__m128i*)psin, phs3);
    _mm_storeu_si128((__m128i*)pcos, phc4);
}


__attribute__((optimize("O3", "inline"), target("avx2"), unused))
static void isincos_avx2(__m256i* pph, __m256i* psin, __m256i *pcos)
{
    __m256i ph = _mm256_loadu_si256((__m256i*)pph);
#ifdef CORR_32768
    __m256i amsk = _mm256_cmpeq_epi16(ph, _mm256_set1_epi16(-32768));
#endif
    __m256i ph2 = _mm256_mulhrs_epi16(ph, ph);
    __m256i phx1 = _mm256_mulhrs_epi16(ph, _mm256_set1_epi16(18705));
    __m256i phx3_c = _mm256_mulhrs_epi16(ph, _mm256_set1_epi16(-21166));
    __m256i phx5_c = _mm256_mulhrs_epi16(ph, _mm256_set1_epi16(2611));
    __m256i phx7_c = _mm256_mulhrs_epi16(ph, _mm256_set1_epi16(-152));
    __m256i ph4 = _mm256_mulhrs_epi16(ph2, ph2);
    __m256i phx3 = _mm256_mulhrs_epi16(ph2, phx3_c);
    __m256i phy2 = _mm256_mulhrs_epi16(ph2, _mm256_set1_epi16(-7656));
    __m256i phs0 = _mm256_add_epi16(ph, phx1);
    __m256i phc0 = _mm256_sub_epi16(_mm256_set1_epi16(32767), ph2);
    __m256i phs1 = _mm256_add_epi16(phs0, phx3);
    __m256i phc1 = _mm256_add_epi16(phc0, phy2);
    __m256i ph6 = _mm256_mulhrs_epi16(ph4, ph2);
    __m256i phx5 = _mm256_mulhrs_epi16(ph4, phx5_c);
    __m256i phy48 = _mm256_mulhrs_epi16(ph4, _mm256_set1_epi16(30));
    __m256i phy4 = _mm256_mulhrs_epi16(ph4, _mm256_set1_epi16(8311));
#ifdef CORR_32768
    __m256i ccorr = _mm256_and_si256(amsk, _mm256_set1_epi16(-57));
    __m256i scorr = _mm256_and_si256(amsk, _mm256_set1_epi16(-28123));
#else
    // dummy
#endif
    __m256i phy6 = _mm256_mulhrs_epi16(ph6, _mm256_set1_epi16(-683));
    __m256i phx7 = _mm256_mulhrs_epi16(ph6, phx7_c);
    __m256i phy8 = _mm256_mulhrs_epi16(ph4, phy48);
    __m256i phs2 = _mm256_add_epi16(phs1, phx5);
    __m256i phc2 = _mm256_add_epi16(phc1, phy4);
#ifdef CORR_32768
    phs2 = _mm256_add_epi16(phs2, scorr);
    phc2 = _mm256_add_epi16(phc2, ccorr);
#else
    // dummy
    // dummy
#endif
    __m256i phs3 = _mm256_add_epi16(phs2, phx7);
    __m256i phc3 = _mm256_add_epi16(phc2, phy6);
    __m256i phc4 = _mm256_add_epi16(phc3, phy8);

    _mm256_storeu_si256((__m256i*)psin, phs3);
    _mm256_storeu_si256((__m256i*)pcos, phc4);
}
#endif



#endif
