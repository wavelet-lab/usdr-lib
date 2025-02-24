#define WVLT_SINCOS(rph, rs, rc) \
    { \
        __m128i amsk = _mm_cmpeq_epi16(rph, _mm_set1_epi16(-32768)); \
        __m128i ccorr = _mm_and_si128(amsk, _mm_set1_epi16(-57)); \
        __m128i scorr = _mm_and_si128(amsk, _mm_set1_epi16(-28123)); \
        \
        __m128i ph2 = _mm_mulhrs_epi16(rph, rph); \
        __m128i phx1 = _mm_mulhrs_epi16(rph, _mm_set1_epi16(18705)); \
        __m128i phx3_c = _mm_mulhrs_epi16(rph, _mm_set1_epi16(-21166)); \
        __m128i phx5_c = _mm_mulhrs_epi16(rph, _mm_set1_epi16(2611)); \
        __m128i phx7_c = _mm_mulhrs_epi16(rph, _mm_set1_epi16(-152)); \
        __m128i ph4 = _mm_mulhrs_epi16(ph2, ph2); \
        __m128i phx3 = _mm_mulhrs_epi16(ph2, phx3_c); \
        __m128i phy2 = _mm_mulhrs_epi16(ph2, _mm_set1_epi16(-7656)); \
        __m128i phs0 = _mm_add_epi16(rph, phx1); \
        __m128i phc0 = _mm_sub_epi16(_mm_set1_epi16(32767), ph2); \
        __m128i phs1 = _mm_add_epi16(phs0, phx3); \
        __m128i phc1 = _mm_add_epi16(phc0, phy2); \
        __m128i ph6 = _mm_mulhrs_epi16(ph4, ph2); \
        __m128i phx5 = _mm_mulhrs_epi16(ph4, phx5_c); \
        __m128i phy48 = _mm_mulhrs_epi16(ph4, _mm_set1_epi16(30)); \
        __m128i phy4 = _mm_mulhrs_epi16(ph4, _mm_set1_epi16(8311)); \
        \
        __m128i phy6 = _mm_mulhrs_epi16(ph6, _mm_set1_epi16(-683)); \
        __m128i phx7 = _mm_mulhrs_epi16(ph6, phx7_c); \
        __m128i phy8 = _mm_mulhrs_epi16(ph4, phy48); \
        __m128i phs2 = _mm_add_epi16(phs1, phx5); \
        __m128i phc2 = _mm_add_epi16(phc1, phy4); \
        \
        phs2 = _mm_add_epi16(phs2, scorr); \
        phc2 = _mm_add_epi16(phc2, ccorr); \
        \
        rs = _mm_add_epi16(phs2, phx7); \
        __m128i phc3 = _mm_add_epi16(phc2, phy6); \
        rc = _mm_add_epi16(phc3, phy8); \
    }
//  WVLT_SINCOS
