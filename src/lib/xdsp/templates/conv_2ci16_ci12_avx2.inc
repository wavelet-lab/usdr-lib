#define STORE_2CI16_CI12_BLOCK(v0, v1) \
{ \
    __m256i i0 = _mm256_castpd_si256(_mm256_shuffle_pd(_mm256_castsi256_pd(v0), _mm256_castsi256_pd(v1), 0b0000)); \
    __m256i i1 = _mm256_castpd_si256(_mm256_shuffle_pd(_mm256_castsi256_pd(v0), _mm256_castsi256_pd(v1), 0b1111)); \
\
    i0 = _mm256_shuffle_epi32(i0, _MM_SHUFFLE(3,1,2,0)); \
    i1 = _mm256_shuffle_epi32(i1, _MM_SHUFFLE(3,1,2,0)); \
\
    __m256i z0 = _mm256_permute2x128_si256(i0, i1, 0b00100000); \
    __m256i z1 = _mm256_permute2x128_si256(i0, i1, 0b00110001); \
\
    CONVERT_I16_I12_BLOCK(z0, out64); \
    CONVERT_I16_I12_BLOCK(z1, out64); \
}
// STORE_2CI16_CI12_BLOCK end