const __m256i maske = _mm256_set1_epi64x(0x0000fff00000fff0);
const __m256i masko = _mm256_set1_epi64x(0xfff00000fff00000);

const __m256i shfl0 = _mm256_set_epi8(
    0x80, 0x80, 0x80, 0x80, 0x0f, 0x0e, 0x0d, 0x0b,
    0x0a, 0x09, 0x07, 0x06, 0x05, 0x03, 0x02, 0x01,
    0x80, 0x80, 0x80, 0x80, 0x0f, 0x0e, 0x0d, 0x0b,
    0x0a, 0x09, 0x07, 0x06, 0x05, 0x03, 0x02, 0x01);

const __m256i permmask0 = _mm256_set_epi32(7,3,6,5,4,2,1,0);
const __m256i storemask0 = _mm256_set_epi64x(0, -1, -1, -1);

#define CONVERT_I16_I12_BLOCK(rin, pout64) \
{ \
    __m256i ro0 = _mm256_and_si256(rin, masko); \
    __m256i re0 = _mm256_slli_epi64(_mm256_and_si256(rin, maske), 4); \
    __m256i r0  = _mm256_or_si256(ro0, re0); \
\
    __m256i res  = _mm256_shuffle_epi8(r0, shfl0); \
    res  = _mm256_permutevar8x32_epi32(res, permmask0); \
\
    _mm256_maskstore_epi64((long long *)pout64, storemask0, res); \
    pout64 += 3; \
}
