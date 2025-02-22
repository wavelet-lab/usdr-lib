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

    const __m256i maske = _mm256_set1_epi64x(0x0000fff00000fff0);
    const __m256i masko = _mm256_set1_epi64x(0xfff00000fff00000);

    const __m256i shfl = _mm256_set_epi8(
        0x80, 0x80, 0x80, 0x80, 0x0f, 0x0e, 0x0d, 0x0b,
        0x0a, 0x09, 0x07, 0x06, 0x05, 0x03, 0x02, 0x01,
        0x80, 0x80, 0x80, 0x80, 0x0f, 0x0e, 0x0d, 0x0b,
        0x0a, 0x09, 0x07, 0x06, 0x05, 0x03, 0x02, 0x01);

    const __m256i permmask0 = _mm256_set_epi32(7,3,6,5,4,2,1,0);
    const __m256i permmask1 = _mm256_set_epi32(7,3,5,1,6,2,4,0);
    const __m256i storemask = _mm256_set_epi64x(0, -1, -1, -1);

#define CONVERT_I16_I12_BLOCK(reg, res) \
    { \
        __m256i ro0 = _mm256_and_si256(reg, masko); \
        __m256i re0 = _mm256_slli_epi64(_mm256_and_si256(reg, maske), 4); \
        __m256i r0  = _mm256_or_si256(ro0, re0); \
    \
        res  = _mm256_shuffle_epi8(r0, shfl); \
        res  = _mm256_permutevar8x32_epi32(res, permmask0); \
    }

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

        __m256i res0, res1, res2, res3;
        CONVERT_I16_I12_BLOCK(i0, res0);
        CONVERT_I16_I12_BLOCK(i1, res1);
        CONVERT_I16_I12_BLOCK(i2, res2);
        CONVERT_I16_I12_BLOCK(i3, res3);

        _mm256_maskstore_epi64((long long*)(out64 + 0), storemask, res0); \
        _mm256_maskstore_epi64((long long*)(out64 + 3), storemask, res1); \
        _mm256_maskstore_epi64((long long*)(out64 + 6), storemask, res2); \
        _mm256_maskstore_epi64((long long*)(out64 + 9), storemask, res3); \
        out64 += 12; \

        indata_0 += 16;
        indata_1 += 16;
        indata_2 += 16;
        indata_3 += 16;

        i -= 32*4;
    }

#undef CONVERT_I16_I12_BLOCK

    uint8_t* outdata = (uint8_t*)out64;

    for (; i >= 16; i -= 16) {

        const int16_t i0 = *indata_0++;
        const int16_t q0 = *indata_0++;
        const int16_t i1 = *indata_1++;
        const int16_t q1 = *indata_1++;
        const int16_t i2 = *indata_2++;
        const int16_t q2 = *indata_2++;
        const int16_t i3 = *indata_3++;
        const int16_t q3 = *indata_3++;

        wu_i16u32_t a0 = {{i0, q0}};
        wu_i16u32_t a1 = {{i1, q1}};
        wu_i16u32_t a2 = {{i2, q2}};
        wu_i16u32_t a3 = {{i3, q3}};

        wu_u32b_t  c0 = {(a0.u & 0xfff00000) | ((a0.u << 4) & 0x000fff00)};
        wu_u32b_t  c1 = {(a1.u & 0xfff00000) | ((a1.u << 4) & 0x000fff00)};
        wu_u32b_t  c2 = {(a2.u & 0xfff00000) | ((a2.u << 4) & 0x000fff00)};
        wu_u32b_t  c3 = {(a3.u & 0xfff00000) | ((a3.u << 4) & 0x000fff00)};

        const wu_u32b_t arr[] = {c0, c1, c2, c3};
        for(unsigned j = 0; j < 4; ++j)
        {
            *(outdata++) = arr[j].b[1];
            *(outdata++) = arr[j].b[2];
            *(outdata++) = arr[j].b[3];
        }
    }
}
#undef TEMPLATE_FUNC_NAME
