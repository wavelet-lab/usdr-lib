const __m512i permmask0 = _mm512_set_epi32(15,14,13,12, 11,5,10,4, 9,3,8,2, 7,1,6,0);
const __m512i permmask1 = _mm512_set_epi64(13,12,5,4,9,8,1,0);
const __m512i permmask2 = _mm512_set_epi64(15,14,7,6,11,10,3,2);

#define AVX512_INTERLEAVE6(in0, in1, out0, out1) \
{ \
    __m512i a0 = _mm512_unpacklo_epi32(in0, in1); \
    __m512i a1 = _mm512_unpackhi_epi32(in0, in1); \
    __m512i b0 = _mm512_castpd_si512(_mm512_shuffle_pd(_mm512_castsi512_pd(a0), _mm512_castsi512_pd(a1), 0b00000000)); \
    __m512i b1 = _mm512_castpd_si512(_mm512_shuffle_pd(_mm512_castsi512_pd(a0), _mm512_castsi512_pd(a1), 0b11111111)); \
          out0 = _mm512_permutexvar_epi32(permmask0, b0); \
          out1 = _mm512_permutexvar_epi32(permmask0, b1); \
}
