static inline
void TEMPLATE_FUNC_NAME(const void *__restrict indata_p,
                        unsigned indatabsz,
                        void *__restrict outdata_0_p,
                        void *__restrict outdata_1_p,
                        unsigned outdatabsz)
{
    unsigned i = indatabsz;
    /* 12 bits -> 16 bits  =>  3 -> 4   */
    if ((outdatabsz * 3 / 4) < i)
        i = (outdatabsz * 3 / 4);

    const uint64_t *in = (const uint64_t*)indata_p;
    int16_t* outdata_0 = (int16_t*)outdata_0_p;
    int16_t* outdata_1 = (int16_t*)outdata_1_p;

    //AVX512BW block
    {
        #include "conv_i12_i16_avx512bw.inc"

        __m512i y0, y1;
        __m512i rs0, rs1;

        const __m512i imask0 = _mm512_set_epi32(30,28,26,24,22,20,18,16,14,12,10,8,6,4,2,0);
        const __m512i imask1 = _mm512_set_epi32(31,29,27,25,23,21,19,17,15,13,11,9,7,5,3,1);

        for(; i >= 96; i -= 96)
        {
            y0 = _mm512_maskz_loadu_epi64(0b00111111, (const long long*)(in + 0));
            y1 = _mm512_maskz_loadu_epi64(0b00111111, (const long long*)(in + 6));
            in += 12;

            CONVERT_I12_I16_BLOCK(y0, rs0);
            CONVERT_I12_I16_BLOCK(y1, rs1);

            _mm512_store_si512((__m512i*)outdata_0, _mm512_permutex2var_epi32(rs0, imask0, rs1));
            _mm512_store_si512((__m512i*)outdata_1, _mm512_permutex2var_epi32(rs0, imask1, rs1));
            outdata_0 += 32;
            outdata_1 += 32;
        }

        #undef CONVERT_I12_I16_BLOCK
        #undef CONVERT_I12_2I32_SEPARATED    
        #undef CONVERT_CI12_2CI32_BLOCK_OPT
        #undef CONVERT_CI12_4CI32_BLOCK_OPT
    }

    //AVX2 block
    {
        #include "conv_i12_i16_avx2.inc"

        if(i >= 48)
        {
            const __m256i imask0 = _mm256_set_epi32(14,12,10,8,6,4,2,0);
            const __m256i imask1 = _mm256_set_epi32(15,13,11,9,7,5,3,1);

            __m256i y0 = _mm256_maskload_epi64((const long long*)(in + 0), load_mask);   // 8 1/3
            __m256i y1 = _mm256_maskload_epi64((const long long*)(in + 3), load_mask);   // 8 1/3
            in += 6;
            i -= 48;

            __m256i rs0, rs1;

            CONVERT_I12_I16_BLOCK(y0, rs0);
            CONVERT_I12_I16_BLOCK(y1, rs1);

            _mm256_store_si256((__m256i*)outdata_0, _mm256_permutex2var_epi32(rs0, imask0, rs1));
            _mm256_store_si256((__m256i*)outdata_1, _mm256_permutex2var_epi32(rs0, imask1, rs1)); 
            outdata_0 += 16;
            outdata_1 += 16;
        }

        #undef CONVERT_I12_I16_BLOCK
        #undef CONVERT_I12_2I32_SEPARATED    
        #undef CONVERT_CI12_2CI32_BLOCK_OPT
        #undef CONVERT_CI12_4CI32_BLOCK_OPT
    }

    //Generic block
    {
        const uint8_t *indata = (const uint8_t*)in;
        #include "conv_ci12_2ci16_generic.inc"
    }
}
#undef TEMPLATE_FUNC_NAME
