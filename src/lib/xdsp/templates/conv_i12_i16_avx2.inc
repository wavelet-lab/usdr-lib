const __m256i load_mask = _mm256_set_epi64x(0, -1, -1, -1);

const __m256i mask0 = _mm256_set1_epi64x(0xfff00000fff00000);
UNUSED const __m256i mask1 = _mm256_set1_epi64x(0x0000fff00000fff0);

const __m256i permmask = _mm256_set_epi32(5, 4, 3, 7, 6, 2, 1, 0);
const __m256i shfl = _mm256_set_epi8(
    0x0f, 0x0e, 0x0d, 0x80, 0x0c, 0x0b, 0x0a, 0x80,
    0x09, 0x08, 0x07, 0x80, 0x06, 0x05, 0x04, 0x80,
    0x0b, 0x0a, 0x09, 0x80, 0x08, 0x07, 0x06, 0x80,
    0x05, 0x04, 0x03, 0x80, 0x02, 0x01, 0x00, 0x80);

#define CONVERT_I12_I16_BLOCK(reg, result) \
{   \
    __m256i v0 = _mm256_permutevar8x32_epi32(reg, permmask); \
    __m256i r  = _mm256_shuffle_epi8(v0, shfl); \
    \
    __m256i r0 = _mm256_and_si256(r, mask0); \
    __m256i r1 = _mm256_and_si256(_mm256_srli_epi64(r, 4), mask1); \
            result = _mm256_or_si256(r0, r1); \
}

/*
* input (i12):   |15|14|13|12|11|10| 9| 8| 7| 6| 5| 4| 3| 2| 1| 0|
*
* output(i32):   |14 xx|12 xx|10 xx| 8 xx| 6 xx| 4 xx| 2 xx| 0 xx|
    (hi word)    |15 xx|13 xx|11 xx| 9 xx| 7 xx| 5 xx| 3 xx| 1 xx| - 2CH i32 interleave
*/
#define CONVERT_I12_2I32_SEPARATED(reg, res0, res1) \
{   \
    __m256i v0 = _mm256_permutevar8x32_epi32(reg, permmask); \
    __m256i r  = _mm256_shuffle_epi8(v0, shfl); \
    \
            res1 = _mm256_and_si256(r, mask0); \
            res0 = _mm256_and_si256(_mm256_slli_epi64(r, 12), mask0); \
}

/*
* input (ci12):  |15|14|13|12|11|10| 9| 8| 7| 6| 5| 4| 3| 2| 1| 0|
*
* output(ci32):  |13 xx|12 xx| 9 xx| 8 xx| 5 xx| 4 xx| 1 xx| 0 xx|
    (hi word)    |15 xx|14 xx|11 xx|10 xx| 7 xx| 6 xx| 3 xx| 2 xx| - 2CH ci32 interleave
*/
#define CONVERT_CI12_2CI32_BLOCK_OPT(reg, res0, res1) \
{   \
    __m256i a0, a1; \
    CONVERT_I12_2I32_SEPARATED(reg, a0, a1); \
    \
    __m256i b0 = _mm256_unpacklo_epi32(a0, a1); \
    __m256i b1 = _mm256_unpackhi_epi32(a0, a1); \
            res0 = _mm256_castpd_si256(_mm256_shuffle_pd(_mm256_castsi256_pd(b0), _mm256_castsi256_pd(b1), 0b0000)); \
            res1 = _mm256_castpd_si256(_mm256_shuffle_pd(_mm256_castsi256_pd(b0), _mm256_castsi256_pd(b1), 0b1111)); \
}

/*
* input (ci12):  |15|14|13|12|11|10| 9| 8| 7| 6| 5| 4| 3| 2| 1| 0|
*                |31|30|29|28|27|26|25|24|23|22|21|20|19|18|17|16|
*
* output(ci32):  |25 xx|24 xx|17 xx|16 xx| 9 xx| 8 xx| 1 xx| 0 xx|
    (hi word)    |27 xx|26 xx|19 xx|18 xx|11 xx|10 xx| 3 xx| 2 xx|
    (hi word)    |29 xx|28 xx|21 xx|20 xx|13 xx|12 xx| 5 xx| 4 xx|
    (hi word)    |31 xx|30 xx|23 xx|22 xx|15 xx|14 xx| 7 xx| 6 xx| - 4CH ci32 interleave
*/
#define CONVERT_CI12_4CI32_BLOCK_OPT(reg0, reg1, res0, res1, res2, res3) \
{   \
    __m256i a0, a1, a2, a3; \
    CONVERT_I12_2I32_SEPARATED(reg0, a0, a1); \
    CONVERT_I12_2I32_SEPARATED(reg1, a2, a3); \
    \
    __m256i b0 = _mm256_unpacklo_epi32(a0, a1); \
    __m256i b1 = _mm256_unpackhi_epi32(a0, a1); \
    __m256i b2 = _mm256_unpacklo_epi32(a2, a3); \
    __m256i b3 = _mm256_unpackhi_epi32(a2, a3); \
    \
    __m256i c0 = _mm256_castpd_si256(_mm256_shuffle_pd(_mm256_castsi256_pd(b0), _mm256_castsi256_pd(b2), 0b0000)); \
    __m256i c1 = _mm256_castpd_si256(_mm256_shuffle_pd(_mm256_castsi256_pd(b0), _mm256_castsi256_pd(b2), 0b1111)); \
    __m256i c2 = _mm256_castpd_si256(_mm256_shuffle_pd(_mm256_castsi256_pd(b1), _mm256_castsi256_pd(b3), 0b0000)); \
    __m256i c3 = _mm256_castpd_si256(_mm256_shuffle_pd(_mm256_castsi256_pd(b1), _mm256_castsi256_pd(b3), 0b1111)); \
    \
            res0 = _mm256_permute4x64_epi64(c0, _MM_SHUFFLE(3,1,2,0)); \
            res1 = _mm256_permute4x64_epi64(c1, _MM_SHUFFLE(3,1,2,0)); \
            res2 = _mm256_permute4x64_epi64(c2, _MM_SHUFFLE(3,1,2,0)); \
            res3 = _mm256_permute4x64_epi64(c3, _MM_SHUFFLE(3,1,2,0)); \
}

