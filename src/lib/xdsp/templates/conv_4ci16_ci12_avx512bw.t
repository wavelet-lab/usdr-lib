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

    //AVX512 block
    {
        #include "conv_i16_i12_avx512bw.inc"

        __m512i  v0, v1, v2, v3;
        const __m512i idx = _mm512_set_epi32(15,11,7,3, 14,10,6,2, 13,9,5,1, 12,8,4,0);
        const __m512i imask0 = _mm512_set_epi32(30,14,28,12,26,10,24,8,22,6,20,4,18,2,16,0);
        const __m512i imask1 = _mm512_set_epi32(31,15,29,13,27,11,25,9,23,7,21,5,19,3,17,1);

        for (; i >= 64*4; i -= 64*4)
        {
            v0 = _mm512_loadu_si512((__m512i*)indata_0);
            v1 = _mm512_loadu_si512((__m512i*)indata_1);
            v2 = _mm512_loadu_si512((__m512i*)indata_2);
            v3 = _mm512_loadu_si512((__m512i*)indata_3);

            indata_0 += 32;
            indata_1 += 32;
            indata_2 += 32;
            indata_3 += 32;

            __m512i a0  = _mm512_permutexvar_epi32(idx, v0);
            __m512i a1  = _mm512_permutexvar_epi32(idx, v1);
            __m512i a2  = _mm512_permutexvar_epi32(idx, v2);
            __m512i a3  = _mm512_permutexvar_epi32(idx, v3);

            __m512i b0 = _mm512_castpd_si512(_mm512_shuffle_pd(_mm512_castsi512_pd(a0), _mm512_castsi512_pd(a2), 0b00000000));
            __m512i b2 = _mm512_castpd_si512(_mm512_shuffle_pd(_mm512_castsi512_pd(a0), _mm512_castsi512_pd(a2), 0b11111111));
            __m512i b1 = _mm512_castpd_si512(_mm512_shuffle_pd(_mm512_castsi512_pd(a1), _mm512_castsi512_pd(a3), 0b00000000));
            __m512i b3 = _mm512_castpd_si512(_mm512_shuffle_pd(_mm512_castsi512_pd(a1), _mm512_castsi512_pd(a3), 0b11111111));

            __m512i c0  = _mm512_permutex2var_epi32(b0, imask0, b1);
            __m512i c1  = _mm512_permutex2var_epi32(b0, imask1, b1);
            __m512i c2  = _mm512_permutex2var_epi32(b2, imask0, b3);
            __m512i c3  = _mm512_permutex2var_epi32(b2, imask1, b3);

            CONVERT_I16_I12_BLOCK(c0, out64);
            CONVERT_I16_I12_BLOCK(c1, out64);
            CONVERT_I16_I12_BLOCK(c2, out64);
            CONVERT_I16_I12_BLOCK(c3, out64);
        }

        #undef CONVERT_I16_I12_BLOCK
    }

    //AVX2 block
    {
        #include "conv_i16_i12_avx2.inc"

        if(i >= 32*4)
        {
            const __m256i permmask1 = _mm256_set_epi32(7,3,5,1,6,2,4,0);

            __m256i v0 = _mm256_loadu_si256((__m256i*)indata_0);
            __m256i v1 = _mm256_loadu_si256((__m256i*)indata_1);
            __m256i v2 = _mm256_loadu_si256((__m256i*)indata_2);
            __m256i v3 = _mm256_loadu_si256((__m256i*)indata_3);

            __m256i z0 = _mm256_permute2x128_si256(v0, v1, 0b00100000);
            __m256i z2 = _mm256_permute2x128_si256(v0, v1, 0b00110001);
            __m256i z1 = _mm256_permute2x128_si256(v2, v3, 0b00100000);
            __m256i z3 = _mm256_permute2x128_si256(v2, v3, 0b00110001);

            __m256i i0 = _mm256_castpd_si256(_mm256_shuffle_pd(_mm256_castsi256_pd(z0), _mm256_castsi256_pd(z1), 0b0000));
            __m256i i1 = _mm256_castpd_si256(_mm256_shuffle_pd(_mm256_castsi256_pd(z0), _mm256_castsi256_pd(z1), 0b1111));
            __m256i i2 = _mm256_castpd_si256(_mm256_shuffle_pd(_mm256_castsi256_pd(z2), _mm256_castsi256_pd(z3), 0b0000));
            __m256i i3 = _mm256_castpd_si256(_mm256_shuffle_pd(_mm256_castsi256_pd(z2), _mm256_castsi256_pd(z3), 0b1111));

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
    }

    //Generic block
    {
        uint8_t* outdata = (uint8_t*)out64;
        #include "conv_4ci16_ci12_generic.inc"
    }
}
#undef TEMPLATE_FUNC_NAME
