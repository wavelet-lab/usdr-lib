static
void TEMPLATE_FUNC_NAME(const void *__restrict indata_0_p,
                        const void *__restrict indata_1_p,
                        const void *__restrict indata_2_p,
                        const void *__restrict indata_3_p,
                        unsigned indatabsz,
                        void *__restrict outdata_p,
                        unsigned outdatabsz)
{
    unsigned i = indatabsz;
    if ((outdatabsz * 4 / 3) < i)
        i = (outdatabsz * 4 / 3);

    const int16_t* indata_0 = (const int16_t*)indata_0_p;
    const int16_t* indata_1 = (const int16_t*)indata_1_p;
    const int16_t* indata_2 = (const int16_t*)indata_2_p;
    const int16_t* indata_3 = (const int16_t*)indata_3_p;

    uint64_t *out64 = (uint64_t*)outdata_p;

    const __m256i permmask1 = _mm256_set_epi32(7,3,5,1,6,2,4,0);

#include "conv_i16_i12_avx2.inc"

    while (i >= 32*4)
    {
        __m256i v0 = _mm256_loadu_si256((__m256i*)indata_0);
        __m256i v1 = _mm256_loadu_si256((__m256i*)indata_1);
        __m256i v2 = _mm256_loadu_si256((__m256i*)indata_2);
        __m256i v3 = _mm256_loadu_si256((__m256i*)indata_3);

        __m256i z0 = _mm256_permute2x128_si256(v0, v1, 0b00100000);
        __m256i z2 = _mm256_permute2x128_si256(v0, v1, 0b00110001);
        __m256i z1 = _mm256_permute2x128_si256(v2, v3, 0b00100000);
        __m256i z3 = _mm256_permute2x128_si256(v2, v3, 0b00110001);

        __m256i i0 = _mm256_castpd_si256(_mm256_shuffle_pd(_mm256_castsi256_pd(z0), _mm256_castsi256_pd(z1), 0b0000)); \
        __m256i i1 = _mm256_castpd_si256(_mm256_shuffle_pd(_mm256_castsi256_pd(z0), _mm256_castsi256_pd(z1), 0b1111)); \
        __m256i i2 = _mm256_castpd_si256(_mm256_shuffle_pd(_mm256_castsi256_pd(z2), _mm256_castsi256_pd(z3), 0b0000)); \
        __m256i i3 = _mm256_castpd_si256(_mm256_shuffle_pd(_mm256_castsi256_pd(z2), _mm256_castsi256_pd(z3), 0b1111)); \

        i0 = _mm256_permutevar8x32_epi32(i0, permmask1);
        i1 = _mm256_permutevar8x32_epi32(i1, permmask1);
        i2 = _mm256_permutevar8x32_epi32(i2, permmask1);
        i3 = _mm256_permutevar8x32_epi32(i3, permmask1);

        /* Convert linear data to CI12 */

        CONVERT_I16_I12_BLOCK(i0, out64);
        CONVERT_I16_I12_BLOCK(i1, out64);
        CONVERT_I16_I12_BLOCK(i2, out64);
        CONVERT_I16_I12_BLOCK(i3, out64);

        indata_0 += 16;
        indata_1 += 16;
        indata_2 += 16;
        indata_3 += 16;

        i -= 32*4;
    }

#undef CONVERT_I16_I12_BLOCK

    uint8_t* outdata = (uint8_t*)out64;
    #include "conv_4ci16_ci12_generic.inc"
}
#undef TEMPLATE_FUNC_NAME
