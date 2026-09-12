/*
*  r0-r1:
*  |              (3)              |              (2)              |              (1)              |              (0)              |
*  +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
*  | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 |
*  +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
*  | 0   0   0   0   0   0   0   0 | f15 | f14 | f13 | f12 | f11 | f10 | f9  | f8  | f7  | f6  | f5  | f4  | f3  | f2  | f1  | f0  |
*  | 0   0   0   0   0   0   0   0 | f31 | f30 | f29 | f28 | f27 | f26 | f25 | f24 | f23 | f22 | f21 | f20 | f19 | f18 | f17 | f16 |
*
*  y0 -y1: _mm256_permutevar8x32_epi32
*  |                               |                               |                               |                               |
*  | f15 | f14 | f13 | f12 | f11 | f10 | f9  | f8  | 0   0   0   0   0   0   0   0 | f7  | f6  | f5  | f4  | f3  | f2  | f1  | f0  |
*  | f31 | f30 | f29 | f28 | f27 | f26 | f25 | f24 | 0   0   0   0   0   0   0   0 | f23 | f22 | f21 | f20 | f19 | f18 | f17 | f16 |
*
*  y0-y2: _mm256_shuffle_epi8
*  |                               |                               |                               |                               |
*  | f15 | f14 | 0 | f13 | f12 | 0 | f11 | f10 | 0 | f9  | f8  | 0 | f7  | f6  | 0 | f5  | f4  | 0 | f3  | f2  | 0 | f1  | f0  | 0 |
*  | f31 | f30 | 0 | f29 | f28 | 0 | f27 | f26 | 0 | f25 | f24 | 0 | f23 | f22 | 0 | f21 | f20 | 0 | f19 | f18 | 0 | f17 | f16 | 0 |
*
*  a0-a1:
*  |                               |                               |                               |                               |
*  | f15 |0| 00 00 | f13 |0| 00 00 | f11 |0| 00 00 | f9  |0| 00 00 | f7  |0| 00 00 | f5  |0| 00 00 | f3  |0| 00 00 | f1  |0| 00 00 |
*  | 00 00 | f14 |0| 00 00 | f12 |0| 00 00 | f10 |0| 00 00 | f8  |0| 00 00 | f6  |0| 00 00 | f4  |0| 00 00 | f2  |0| 00 00 | f0  |0|
*
*  i0: _mm256_or_si256(a0, a1)
*  i1: _mm256_or_si256(b0, b1)
*  |                               |                               |                               |                               |
*  | f15 |0| f14 |0| f13 |0| f12 |0| f11 |0| f10 |0| f9  |0| f8  |0| f7  |0| f6  |0| f5  |0| f4  |0| f3  |0| f2  |0| f1  |0| f0  |0|
*  | f31 |0| f30 |0| f29 |0| f28 |0| f27 |0| f26 |0| f25 |0| f24 |0| f23 |0| f22 |0| f21 |0| f20 |0| f19 |0| f18 |0| f17 |0| f16 |0|
*
*  i0-i1: _mm256_permutevar8x32_epi32
*  |                               |                               |                               |                               |
*  | f15 |0| f14 |0| f7  |0| f6  |0| f11 |0| f10 |0| f3  |0| f2  |0| f13 |0| f12 |0| f5  |0| f4  |0| f9  |0| f8  |0| f1  |0| f0  |0|
*  | f31 |0| f30 |0| f23 |0| f22 |0| f27 |0| f26 |0| f19 |0| f18 |0| f29 |0| f28 |0| f21 |0| f20 |0| f25 |0| f24 |0| f17 |0| f16 |0|
*
*  z0-z1: _mm256_castpd_si256
*  |                               |                               |                               |                               |
*  | f27 |0| f26 |0| f19 |0| f18 |0| f11 |0| f10 |0| f3  |0| f2  |0| f25 |0| f24 |0| f17 |0| f16 |0| f9  |0| f8  |0| f1  |0| f0  |0|
*  | f31 |0| f30 |0| f23 |0| f22 |0| f15 |0| f14 |0| f7  |0| f6  |0| f29 |0| f28 |0| f21 |0| f20 |0| f13 |0| f12 |0| f5  |0| f4  |0|
*/

const __m256  scale = _mm256_set1_ps(CONV_SCALE);
const __m256i load_mask = _mm256_set_epi64x(0, -1, -1, -1);

const __m256i mask0 = _mm256_set1_epi64x(0xfff00000fff00000);
const __m256i mask1 = _mm256_set1_epi64x(0x0000fff00000fff0);

const __m256i permmask0 = _mm256_set_epi32(5, 4, 3, 7, 6, 2, 1, 0);
const __m256i permmask1 = _mm256_set_epi32(7, 3, 5, 1, 6, 2, 4, 0);

const __m256i shfl = _mm256_set_epi8(
    0x0f, 0x0e, 0x0d, 0x80, 0x0c, 0x0b, 0x0a, 0x80,
    0x09, 0x08, 0x07, 0x80, 0x06, 0x05, 0x04, 0x80,
    0x0b, 0x0a, 0x09, 0x80, 0x08, 0x07, 0x06, 0x80,
    0x05, 0x04, 0x03, 0x80, 0x02, 0x01, 0x00, 0x80);

#define CONVERT_CI12_4F32_BLOCK(y0, y1) \
{ \
    y0 = _mm256_permutevar8x32_epi32(y0, permmask0); \
    y1 = _mm256_permutevar8x32_epi32(y1, permmask0); \
    \
    y0 = _mm256_shuffle_epi8(y0, shfl); \
    y1 = _mm256_shuffle_epi8(y1, shfl); \
    \
    __m256i a0 = _mm256_and_si256(y0, mask0); \
    __m256i b0 = _mm256_and_si256(y1, mask0); \
    \
    __m256i a1 = _mm256_and_si256(_mm256_srli_epi64(y0, 4), mask1); \
    __m256i b1 = _mm256_and_si256(_mm256_srli_epi64(y1, 4), mask1); \
    \
    __m256i i0 = _mm256_or_si256(a0, a1); \
    __m256i i1 = _mm256_or_si256(b0, b1); \
    \
    /* Linear I12->F32 conv completed here */ \
    /* Next section dedicated to 4-way interleave processing */ \
    \
    i0 = _mm256_permutevar8x32_epi32(i0, permmask1); \
    i1 = _mm256_permutevar8x32_epi32(i1, permmask1); \
    \
    __m256i z0 = _mm256_castpd_si256(_mm256_shuffle_pd(_mm256_castsi256_pd(i0), _mm256_castsi256_pd(i1), 0b0000)); \
    __m256i z1 = _mm256_castpd_si256(_mm256_shuffle_pd(_mm256_castsi256_pd(i0), _mm256_castsi256_pd(i1), 0b1111)); \
    \
    __m256i d0 = _mm256_cvtepi16_epi32(_mm256_castsi256_si128(z0)); \
    __m256i d1 = _mm256_cvtepi16_epi32(_mm256_extracti128_si256(z0, 1)); \
    __m256i d2 = _mm256_cvtepi16_epi32(_mm256_castsi256_si128(z1)); \
    __m256i d3 = _mm256_cvtepi16_epi32(_mm256_extracti128_si256(z1, 1)); \
    \
    _mm256_storeu_ps(outdata_0, _mm256_mul_ps(_mm256_cvtepi32_ps(d0), scale)); \
    _mm256_storeu_ps(outdata_1, _mm256_mul_ps(_mm256_cvtepi32_ps(d1), scale)); \
    _mm256_storeu_ps(outdata_2, _mm256_mul_ps(_mm256_cvtepi32_ps(d2), scale)); \
    _mm256_storeu_ps(outdata_3, _mm256_mul_ps(_mm256_cvtepi32_ps(d3), scale)); \
    \
    outdata_0 += 8; \
    outdata_1 += 8; \
    outdata_2 += 8; \
    outdata_3 += 8; \
}
