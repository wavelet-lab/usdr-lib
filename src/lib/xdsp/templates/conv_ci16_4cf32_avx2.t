static
void TEMPLATE_FUNC_NAME(const void *__restrict indata,
                        unsigned indatabsz,
                        void *__restrict outdata_0_p,
                        void *__restrict outdata_1_p,
                        void *__restrict outdata_2_p,
                        void *__restrict outdata_3_p,
                        unsigned outdatabsz)
{
    unsigned i = indatabsz;
    if ((outdatabsz / 2) < i)
        i = (outdatabsz / 2);

    const uint64_t *ld = (const uint64_t *)indata;

    float* outdata_0 = (float*)outdata_0_p;
    float* outdata_1 = (float*)outdata_1_p;
    float* outdata_2 = (float*)outdata_2_p;
    float* outdata_3 = (float*)outdata_3_p;

/*
*  reg:
*  |              (3)              |              (2)              |              (1)              |              (0)              |
*  +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
*  | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 |
*  +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
*  |       H       |       G       |       F       |       E       |       D       |       C       |       B       |       A       |
*  |  f15  |  f14  |  f13  |  f12  |  f11  |  f10  |  f9   |  f8   |  f7   |  f6   |  f5   |  f4   |  f3   |  f2   |  f1   |  f0   |
*  +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
*  |  f31  |  f30  |  f29  |  f28  |  f27  |  f26  |  f25  |  f24  |  f23  |  f22  |  f21  |  f20  |  f19  |  f18  |  f17  |  f16  |
*  |       P       |       O       |       N       |       M       |       L       |       K       |       J       |       I       |
*
*  lo0      D C B A
*  hi0      H G F E
*  lo1      L K J I
*  hi1      P O N M
*
*  _mm256_permutevar8x32_epi32:                            <----------------------------------------------->
*                          <----------------------------------------------->
*  |       H       |       G       |       F       |       E       |       D       |       C       |       B       |       A       |
*  |       P       |       O       |       N       |       M       |       L       |       K       |       J       |       I       |
*                          <----------------------------------------------->
*                                                          <----------------------------------------------->
*
*  _mm256_shuffle_pd:
*  |       H       |       D       |       F       |       B       |       G       |       C       |       E       |       A       |
*  +-------------------------------+                               +-------------------------------+                               |
*                  \<----------------------------->\                               \<----------------------------->\
*                                  +-------------------------------+                               +-------------------------------+
*  |       P       |       L       |       N       |       J       |       O       |       K       |       M       |       I       |
*
*
*  |       N       |       J       |       F       |       B       |       M       |       I       |       E       |       A       |
*  |  f27  |  f26  |  f19  |  f18  |  f11  |  f10  |  f3   |  f2   |  f25  |  f24  |  f17  |  f16  |  f9   |  f8   |  f1   |  f0   |
*  |  f31  |  f30  |  f23  |  f22  |  f15  |  f14  |  f7   |  f6   |  f29  |  f28  |  f21  |  f20  |  f13  |  f12  |  f5   |  f4   |
*  |       P       |       L       |       H       |       D       |       O       |       K       |       G       |       C       |
*
*  lo0      M I E A
*  hi0      N J F B
*  lo1      O K G C
*  hi1      P L H D
*
*  +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
*  |      f25      |      f24      |      f17      |      f16      |      f9       |      f8       |      f1       |      f0       |
*  |      f27      |      f26      |      f19      |      f18      |      f11      |      f10      |      f3       |      f2       |
*  |      f29      |      f28      |      f21      |      f20      |      f13      |      f12      |      f5       |      f4       |
*  |      f31      |      f30      |      f23      |      f22      |      f15      |      f14      |      f7       |      f6       |
*  +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
*/

    const __m256  scale = _mm256_set1_ps(CONV_SCALE);
    const __m256i permmask = _mm256_set_epi32(7, 3, 5, 1, 6, 2, 4, 0);

#define CONVERT_CI16_4CI32_BLOCK(i0, i1) \
    { \
        i0 = _mm256_permutevar8x32_epi32(i0, permmask); \
        i1 = _mm256_permutevar8x32_epi32(i1, permmask); \
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
// CONVERT_CI16_4CI32_BLOCK

    while(i >= 64)
    {
        __m256i reg0 = _mm256_loadu_si256((__m256i*)(ld + 0));
        __m256i reg1 = _mm256_loadu_si256((__m256i*)(ld + 4));

        CONVERT_CI16_4CI32_BLOCK(reg0, reg1);

        i -= 64;
        ld += 8;
    }

#undef CONVERT_CI16_4CI32_BLOCK

    for (; i >= 16; i -= 16)
    {
        const uint64_t v0 = *(ld++);
        const uint64_t v1 = *(ld++);

        const float i0 = (int16_t)(v0);
        const float q0 = (int16_t)(v0>>16);
        const float i1 = (int16_t)(v0>>32);
        const float q1 = (int16_t)(v0>>48);
        const float i2 = (int16_t)(v1);
        const float q2 = (int16_t)(v1>>16);
        const float i3 = (int16_t)(v1>>32);
        const float q3 = (int16_t)(v1>>48);

        *(outdata_0++) = i0 * CONV_SCALE;
        *(outdata_0++) = q0 * CONV_SCALE;
        *(outdata_1++) = i1 * CONV_SCALE;
        *(outdata_1++) = q1 * CONV_SCALE;
        *(outdata_2++) = i2 * CONV_SCALE;
        *(outdata_2++) = q2 * CONV_SCALE;
        *(outdata_3++) = i3 * CONV_SCALE;
        *(outdata_3++) = q3 * CONV_SCALE;
    }

    // do nothing with leftover
}

#undef TEMPLATE_FUNC_NAME
