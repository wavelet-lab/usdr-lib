const __m512i mask0 = _mm512_set1_epi64(0xfff00000fff00000);
UNUSED const __m512i mask1 = _mm512_set1_epi64(0x0000fff00000fff0);

const __m512i permmask = _mm512_set_epi32(11,10,9, 15,14, 8,7,6, 5,4,3, 13,12, 2,1,0);
const __m512i shfl = _mm512_set_epi8(
    0x0f, 0x0e, 0x0d, 0x80, 0x0c, 0x0b, 0x0a, 0x80,
    0x09, 0x08, 0x07, 0x80, 0x06, 0x05, 0x04, 0x80,
    0x0b, 0x0a, 0x09, 0x80, 0x08, 0x07, 0x06, 0x80,
    0x05, 0x04, 0x03, 0x80, 0x02, 0x01, 0x00, 0x80,
    0x0f, 0x0e, 0x0d, 0x80, 0x0c, 0x0b, 0x0a, 0x80,
    0x09, 0x08, 0x07, 0x80, 0x06, 0x05, 0x04, 0x80,
    0x0b, 0x0a, 0x09, 0x80, 0x08, 0x07, 0x06, 0x80,
    0x05, 0x04, 0x03, 0x80, 0x02, 0x01, 0x00, 0x80);

UNUSED const __m512i pidx = _mm512_set_epi64(7,5,3,1,6,4,2,0);

#define CONVERT_I12_I16_BLOCK(reg, result) \
{   \
    __m512i v0 = _mm512_permutexvar_epi32(permmask, reg); \
    __m512i r  = _mm512_shuffle_epi8(v0, shfl); \
    \
    __m512i r0 = _mm512_and_si512(r, mask0); \
    __m512i r1 = _mm512_and_si512(_mm512_srli_epi64(r, 4), mask1); \
            result = _mm512_or_si512(r0, r1); \
}

#define CONVERT_I12_2I32_SEPARATED(reg, res0, res1) \
{   \
    __m512i v0 = _mm512_permutexvar_epi32(permmask, reg); \
    __m512i r  = _mm512_shuffle_epi8(v0, shfl); \
    \
            res1 = _mm512_and_si512(r, mask0); \
            res0 = _mm512_and_si512(_mm512_slli_epi64(r, 12), mask0); \
}

#define CONVERT_CI12_2CI32_BLOCK_OPT(reg, res0, res1) \
{   \
    __m512i a0, a1; \
    CONVERT_I12_2I32_SEPARATED(reg, a0, a1); \
    \
    __m512i b0 = _mm512_unpacklo_epi32(a0, a1); \
    __m512i b1 = _mm512_unpackhi_epi32(a0, a1); \
            res0 = _mm512_castpd_si512(_mm512_shuffle_pd(_mm512_castsi512_pd(b0), _mm512_castsi512_pd(b1), 0b00000000)); \
            res1 = _mm512_castpd_si512(_mm512_shuffle_pd(_mm512_castsi512_pd(b0), _mm512_castsi512_pd(b1), 0b11111111)); \
}

#define CONVERT_CI12_4CI32_BLOCK_OPT(reg0, reg1, res0, res1, res2, res3) \
{   \
    __m512i a0, a1, a2, a3; \
    CONVERT_I12_2I32_SEPARATED(reg0, a0, a1); \
    CONVERT_I12_2I32_SEPARATED(reg1, a2, a3); \
    \
    __m512i b0 = _mm512_unpacklo_epi32(a0, a1); \
    __m512i b1 = _mm512_unpackhi_epi32(a0, a1); \
    __m512i b2 = _mm512_unpacklo_epi32(a2, a3); \
    __m512i b3 = _mm512_unpackhi_epi32(a2, a3); \
    \
    __m512i c0 = _mm512_castpd_si512(_mm512_shuffle_pd(_mm512_castsi512_pd(b0), _mm512_castsi512_pd(b2), 0b00000000)); \
    __m512i c1 = _mm512_castpd_si512(_mm512_shuffle_pd(_mm512_castsi512_pd(b0), _mm512_castsi512_pd(b2), 0b11111111)); \
    __m512i c2 = _mm512_castpd_si512(_mm512_shuffle_pd(_mm512_castsi512_pd(b1), _mm512_castsi512_pd(b3), 0b00000000)); \
    __m512i c3 = _mm512_castpd_si512(_mm512_shuffle_pd(_mm512_castsi512_pd(b1), _mm512_castsi512_pd(b3), 0b11111111)); \
    \
            res0 = _mm512_permutexvar_epi64(pidx, c0); \
            res1 = _mm512_permutexvar_epi64(pidx, c1); \
            res2 = _mm512_permutexvar_epi64(pidx, c2); \
            res3 = _mm512_permutexvar_epi64(pidx, c3); \
}
