UNUSED const __m256  scale  = _mm256_set1_ps(CONV_SCALE);
UNUSED const __m256  scale2 = _mm256_set1_ps(SCALE2);

/*
*  reg:
*  |              (3)              |              (2)              |              (1)              |              (0)              |
*  +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
*  | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 |
*  +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
*  | 0   0   0   0   0   0   0   0 | f15 | f14 | f13 | f12 | f11 | f10 | f9  | f8  | f7  | f6  | f5  | f4  | f3  | f2  | f1  | f0  |
*
*  v0:
*  |                               |                               |                               |                               |
*  | f15 | f14 | f13 | f12 | f11 | f10 | f9  | f8  | 0   0   0   0   0   0   0   0 | f7  | f6  | f5  | f4  | f3  | f2  | f1  | f0  |
*  r:
*  |                               |                               |                               |                               |
*  | f15 | f14 | 0 | f13 | f12 | 0 | f11 | f10 | 0 | f9  | f8  | 0 | f7  | f6  | 0 | f5  | f4  | 0 | f3  | f2  | 0 | f1  | f0  | 0 |
*  r0:
*  |                               |                               |                               |                               |
*  | f15 |0| 00 00 | f13 |0| 00 00 | f11 |0| 00 00 | f9  |0| 00 00 | f7  |0| 00 00 | f5  |0| 00 00 | f3  |0| 00 00 | f1  |0| 00 00 |
   r1:
*  |                               |                               |                               |                               |
*  | 00 00 | f14 |0| 00 00 | f12 |0| 00 00 | f10 |0| 00 00 | f8  |0| 00 00 | f6  |0| 00 00 | f4  |0| 00 00 | f2  |0| 00 00 | f0  |0|
*  res:
*  |                               |                               |                               |                               |
*  | f15 |0| f14 |0| f13 |0| f12 |0| f11 |0| f10 |0| f9  |0| f8  |0| f7  |0| f6  |0| f5  |0| f4  |0| f3  |0| f2  |0| f1  |0| f0  |0|
*/

/*
*  | f14 |0| 00 00 | f12 |0| 00 00 | f10 |0| 00 00 | f8  |0| 00 00 | f6  |0| 00 00 | f4  |0| 00 00 | f2  |0| 00 00 | f0  |0| 00 00 |
*  | f15 |0| 00 00 | f13 |0| 00 00 | f11 |0| 00 00 | f9  |0| 00 00 | f7  |0| 00 00 | f5  |0| 00 00 | f3  |0| 00 00 | f1  |0| 00 00 |


f14 f12 f10 f8  | f6 f4 f2 f0   after 12-16 conv
f15 f13 f11 f9  | f7 f5 f3 f1

f11 f10 f9  f8  | f3 f2 f1 f0   unpack
f15 f14 f13 f12 | f7 f6 f5 f4

f13 f12 f9  f8  | f5 f4 f1 f0   _mm256_shuffle_pd
f15 f14 f11 f10 | f7 f6 f3 f2

*/

#define CONVERT_I12_F32_BLOCK(reg, res0, res1) \
{   \
    __m256i result; \
    CONVERT_I12_I16_BLOCK(reg, result); \
    \
    __m256i d0 = _mm256_cvtepi16_epi32(_mm256_castsi256_si128(result)); \
    __m256i d1 = _mm256_cvtepi16_epi32(_mm256_extracti128_si256(result, 1)); \
    \
    res0 = _mm256_cvtepi32_ps(d0);                                      /* 4 1|2 */ \
    res1 = _mm256_cvtepi32_ps(d1);                                      /* 4 1|2 */ \
    \
    res0 = _mm256_mul_ps(res0, scale);                                           /* 4 1|2 */ \
    res1 = _mm256_mul_ps(res1, scale);                                           /* 4 1|2 */ \
}

#define CONVERT_I12_F32_BLOCK_STORE1(reg) \
{   \
    __m256 res0, res1; \
    CONVERT_I12_F32_BLOCK(reg, res0, res1); \
    _MM256_STOREX_PS(outdata + 0, res0); \
    _MM256_STOREX_PS(outdata + 8, res1); \
    outdata += 16; \
}

#define CONVERT_CI12_2CF32_BLOCK_OPT(reg, res0, res1) \
{   \
    __m256i result0, result1; \
    CONVERT_CI12_2CI32_BLOCK_OPT(reg, result0, result1); \
    \
    res0 = _mm256_mul_ps(_mm256_cvtepi32_ps(result0), scale2); \
    res1 = _mm256_mul_ps(_mm256_cvtepi32_ps(result1), scale2); \
}

#define CONVERT_CI12_4CF32_BLOCK_OPT(reg0, reg1, res0, res1, res2, res3) \
{   \
    __m256i r0, r1, r2, r3; \
    CONVERT_CI12_4CI32_BLOCK_OPT(reg0, reg1, r0, r1, r2, r3); \
    \
    res0 = _mm256_mul_ps(_mm256_cvtepi32_ps(r0), scale2); \
    res1 = _mm256_mul_ps(_mm256_cvtepi32_ps(r1), scale2); \
    res2 = _mm256_mul_ps(_mm256_cvtepi32_ps(r2), scale2); \
    res3 = _mm256_mul_ps(_mm256_cvtepi32_ps(r3), scale2); \
}
