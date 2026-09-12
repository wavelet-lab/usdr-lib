static
void TEMPLATE_FUNC_NAME(const void *__restrict indata_0_p,
                        const void *__restrict indata_1_p,
                        unsigned indatabsz,
                        void *__restrict outdata_p,
                        unsigned outdatabsz)
{
    unsigned i = indatabsz;
    if ((outdatabsz * 4 / 3) < i)
        i = (outdatabsz * 4 / 3);

    const int16_t* indata_0 = (const int16_t*)indata_0_p;
    const int16_t* indata_1 = (const int16_t*)indata_1_p;
    uint64_t *out64 = (uint64_t*)outdata_p;

    //AVX512 block
    {
        #include "conv_i16_i12_avx512bw.inc"

        const __m512i imask0 = _mm512_set_epi32(23, 7,22, 6,   21, 5,20, 4,   19, 3,18, 2,   17, 1,16, 0);
        const __m512i imask1 = _mm512_set_epi32(31,15,30,14,   29,13,28,12,   27,11,26,10,   25, 9,24, 8);
        __m512i  v0, v1;

        for (; i >= 64*2; i -= 64*2)
        {
            v0 = _mm512_loadu_si512((__m512i*)indata_0);
            v1 = _mm512_loadu_si512((__m512i*)indata_1);
            indata_0 += 32;
            indata_1 += 32;

            __m512i z0  = _mm512_permutex2var_epi32(v0, imask0, v1);
            __m512i z1  = _mm512_permutex2var_epi32(v0, imask1, v1);

            CONVERT_I16_I12_BLOCK(z0, out64);
            CONVERT_I16_I12_BLOCK(z1, out64);
        }

        #undef CONVERT_I16_I12_BLOCK
    }

    //AVX2 block
    {
        #include "conv_i16_i12_avx2.inc"
        #include "conv_2ci16_ci12_avx2.inc"

        __m256i  v0, v1;

        if(i >= 32*2)
        {
            v0 = _mm256_loadu_si256((__m256i*)indata_0);
            v1 = _mm256_loadu_si256((__m256i*)indata_1);
            indata_0 += 16;
            indata_1 += 16;
            i -= 64;

            STORE_2CI16_CI12_BLOCK(v0, v1);
        }

        #undef STORE_2CI16_CI12_BLOCK
        #undef CONVERT_I16_I12_BLOCK
    }

    //Generic block
    {
        uint8_t* outdata = (uint8_t*)out64;
        #include "conv_2ci16_ci12_generic.inc"
    }
}
#undef TEMPLATE_FUNC_NAME
