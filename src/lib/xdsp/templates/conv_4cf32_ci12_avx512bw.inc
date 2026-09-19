const __m512  scale = _mm512_set1_ps(1.0f / CONV_SCALE);
const __m512i imask0 = _mm512_set_epi32(23,21,7,5,   22,20,6,4,    19,17,3,1,  18,16,2,0);
const __m512i imask1 = _mm512_set_epi32(31,29,15,13, 30,28,14,12,  27,25,11,9, 26,24,10,8);

#define CONVERT_4CF32_CI12_BLOCK(f0, f1, f2, f3) \
{ \
    __m512i i0 = _mm512_cvtps_epi32(_mm512_mul_ps(f0, scale)); \
    __m512i i1 = _mm512_cvtps_epi32(_mm512_mul_ps(f1, scale)); \
    __m512i i2 = _mm512_cvtps_epi32(_mm512_mul_ps(f2, scale)); \
    __m512i i3 = _mm512_cvtps_epi32(_mm512_mul_ps(f3, scale)); \
  \
    __m512i ii0 = _mm512_packs_epi32(i0, i1); \
    __m512i ii1 = _mm512_packs_epi32(i2, i3); \
  \
    __m512i x0  = _mm512_permutex2var_epi32(ii0, imask0, ii1); \
    __m512i x1  = _mm512_permutex2var_epi32(ii0, imask1, ii1); \
  \
    /* Convert linear data to CI12 */ \
  \
    CONVERT_I16_I12_BLOCK(x0, out64); \
    CONVERT_I16_I12_BLOCK(x1, out64); \
}
