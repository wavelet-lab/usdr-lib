const __m512 scale = _mm512_set1_ps(1.0f / CONV_SCALE);
const __m512i idx = _mm512_set_epi64(7,5,3,1,6,4,2,0);

#define CONVERT_F32_I12_BLOCK(v0, v1) \
{ \
    v0 = _mm512_mul_ps(v0, scale); \
    v1 = _mm512_mul_ps(v1, scale); \
\
    __m512i i0 = _mm512_cvtps_epi32(v0); \
    __m512i i1 = _mm512_cvtps_epi32(v1); \
\
    __m512i ii0 = _mm512_packs_epi32(i0, i1); \
    ii0 = _mm512_permutexvar_epi64(idx, ii0); \
\
    CONVERT_I16_I12_BLOCK(ii0, out64); \
}
