const __m256  scale = _mm256_set1_ps(1.0f / CONV_SCALE);

#define CONVERT_4CF32_CI12_BLOCK(f0, f1, f2, f3) \
{ \
    __m256i i0 = _mm256_cvtps_epi32(_mm256_mul_ps(f0, scale)); \
    __m256i i1 = _mm256_cvtps_epi32(_mm256_mul_ps(f1, scale)); \
    __m256i i2 = _mm256_cvtps_epi32(_mm256_mul_ps(f2, scale)); \
    __m256i i3 = _mm256_cvtps_epi32(_mm256_mul_ps(f3, scale)); \
  \
    /* 4-way CF32 deinterleave (see conv_4cf32_ci16_avx2.t */ \
  \
    __m256i ii0 = _mm256_shuffle_epi32(_mm256_packs_epi32(i0, i1), _MM_SHUFFLE(3,1,2,0)); \
    __m256i ii1 = _mm256_shuffle_epi32(_mm256_packs_epi32(i2, i3), _MM_SHUFFLE(3,1,2,0)); \
  \
    __m256i z0 = _mm256_castpd_si256(_mm256_shuffle_pd(_mm256_castsi256_pd(ii0), _mm256_castsi256_pd(ii1), 0b0000)); \
    __m256i z1 = _mm256_castpd_si256(_mm256_shuffle_pd(_mm256_castsi256_pd(ii0), _mm256_castsi256_pd(ii1), 0b1111)); \
  \
    __m256i d0 = _mm256_permute2x128_si256(z0, z1, 0b00100000); \
    __m256i d1 = _mm256_permute2x128_si256(z0, z1, 0b00110001); \
  \
    /* Convert linear data to CI12 */ \
  \
    CONVERT_I16_I12_BLOCK(d0, out64); \
    CONVERT_I16_I12_BLOCK(d1, out64); \
}
