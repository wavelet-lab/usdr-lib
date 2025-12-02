const __m256 scale = _mm256_set1_ps(1.0f / CONV_SCALE);

#define CONVERT_F32_I12_BLOCK(v0, v1) \
{ \
    v0 = _mm256_mul_ps(v0, scale); \
    v1 = _mm256_mul_ps(v1, scale); \
\
    __m256i i0 = _mm256_cvtps_epi32(v0); \
    __m256i i1 = _mm256_cvtps_epi32(v1); \
\
    __m256i ii0 = _mm256_packs_epi32(i0, i1); \
    ii0 = _mm256_permute4x64_epi64(ii0, _MM_SHUFFLE(3,1,2,0)); \
\
    CONVERT_I16_I12_BLOCK(ii0, out64); \
}
