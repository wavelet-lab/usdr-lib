UNUSED const __m512  scale = _mm512_set1_ps(CONV_SCALE);
UNUSED const __m512  scale2 = _mm512_set1_ps(SCALE2);

#define CONVERT_I12_F32_BLOCK(reg, res0, res1) \
{   \
    __m512i result; \
    CONVERT_I12_I16_BLOCK(reg, result); \
    \
    __m512i d0 = _mm512_cvtepi16_epi32(_mm512_castsi512_si256(result)); \
    __m512i d1 = _mm512_cvtepi16_epi32(_mm512_extracti64x4_epi64(result, 1)); \
    \
    res0 = _mm512_cvtepi32_ps(d0); \
    res1 = _mm512_cvtepi32_ps(d1); \
    \
    res0 = _mm512_mul_ps(res0, scale); \
    res1 = _mm512_mul_ps(res1, scale); \
}

#define CONVERT_CI12_2CF32_BLOCK_OPT(reg, res0, res1) \
{   \
    __m512i result0, result1; \
    CONVERT_CI12_2CI32_BLOCK_OPT(reg, result0, result1); \
    \
    res0 = _mm512_mul_ps(_mm512_cvtepi32_ps(result0), scale2); \
    res1 = _mm512_mul_ps(_mm512_cvtepi32_ps(result1), scale2); \
}

#define CONVERT_CI12_4CF32_BLOCK_OPT(reg0, reg1, res0, res1, res2, res3) \
{   \
    __m512i r0, r1, r2, r3; \
    CONVERT_CI12_4CI32_BLOCK_OPT(reg0, reg1, r0, r1, r2, r3); \
    \
    res0 = _mm512_mul_ps(_mm512_cvtepi32_ps(r0), scale2); \
    res1 = _mm512_mul_ps(_mm512_cvtepi32_ps(r1), scale2); \
    res2 = _mm512_mul_ps(_mm512_cvtepi32_ps(r2), scale2); \
    res3 = _mm512_mul_ps(_mm512_cvtepi32_ps(r3), scale2); \
}
