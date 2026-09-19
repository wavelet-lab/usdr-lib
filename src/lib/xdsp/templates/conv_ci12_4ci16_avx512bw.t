static
void TEMPLATE_FUNC_NAME(const void *__restrict indata_p,
                        unsigned indatabsz,
                        void *__restrict outdata_0_p,
                        void *__restrict outdata_1_p,
                        void *__restrict outdata_2_p,
                        void *__restrict outdata_3_p,
                        unsigned outdatabsz)
{
    unsigned i = indatabsz;
    /* 12 bits -> 16 bits  =>  3 -> 4   */
    if ((outdatabsz * 3 / 4) < i)
        i = (outdatabsz * 3 / 4);

    const uint64_t *in = (const uint64_t*)indata_p;
    int16_t* outdata_0 = (int16_t*)outdata_0_p;
    int16_t* outdata_1 = (int16_t*)outdata_1_p;
    int16_t* outdata_2 = (int16_t*)outdata_2_p;
    int16_t* outdata_3 = (int16_t*)outdata_3_p;

    //AVX512BW block
    {
        #include "conv_i12_i16_avx512bw.inc"

        __m512i y0, y1, y2, y3;
        __m512i rs0, rs1, rs2, rs3;
        __m512i a0, a1, a2, a3;
        __m512i b0, b1, b2, b3;

        const __m512i imask0 = _mm512_set_epi32(30,14,28,12,26,10,24,8,22,6,20,4,18,2,16,0);
        const __m512i imask1 = _mm512_set_epi32(31,15,29,13,27,11,25,9,23,7,21,5,19,3,17,1);
        const __m512i imask2 = _mm512_set_epi32(15,11,7,3, 14,10,6,2, 13,9,5,1, 12,8,4,0);

        for(; i >= 2*96; i -= 2*96)
        {
            y0 = _mm512_maskz_loadu_epi64(0b00111111, (const long long*)(in +  0));
            y1 = _mm512_maskz_loadu_epi64(0b00111111, (const long long*)(in +  6));
            y2 = _mm512_maskz_loadu_epi64(0b00111111, (const long long*)(in + 12));
            y3 = _mm512_maskz_loadu_epi64(0b00111111, (const long long*)(in + 18));
            in += 24;

            CONVERT_I12_I16_BLOCK(y0, rs0);
            CONVERT_I12_I16_BLOCK(y1, rs1);
            CONVERT_I12_I16_BLOCK(y2, rs2);
            CONVERT_I12_I16_BLOCK(y3, rs3);

            a0  = _mm512_permutex2var_epi32(rs0, imask0, rs1);
            a1  = _mm512_permutex2var_epi32(rs0, imask1, rs1);
            a2  = _mm512_permutex2var_epi32(rs2, imask0, rs3);
            a3  = _mm512_permutex2var_epi32(rs2, imask1, rs3);

            b0 = _mm512_castpd_si512(_mm512_shuffle_pd(_mm512_castsi512_pd(a0), _mm512_castsi512_pd(a2), 0b00000000)); //1
            b2 = _mm512_castpd_si512(_mm512_shuffle_pd(_mm512_castsi512_pd(a0), _mm512_castsi512_pd(a2), 0b11111111)); //1
            b1 = _mm512_castpd_si512(_mm512_shuffle_pd(_mm512_castsi512_pd(a1), _mm512_castsi512_pd(a3), 0b00000000)); //1
            b3 = _mm512_castpd_si512(_mm512_shuffle_pd(_mm512_castsi512_pd(a1), _mm512_castsi512_pd(a3), 0b11111111)); //1

            _mm512_store_si512((__m512i*)outdata_0, _mm512_permutexvar_epi32(imask2, b0));
            _mm512_store_si512((__m512i*)outdata_1, _mm512_permutexvar_epi32(imask2, b1));
            _mm512_store_si512((__m512i*)outdata_2, _mm512_permutexvar_epi32(imask2, b2));
            _mm512_store_si512((__m512i*)outdata_3, _mm512_permutexvar_epi32(imask2, b3));

            outdata_0 += 32;
            outdata_1 += 32;
            outdata_2 += 32;
            outdata_3 += 32;
        }

        #undef CONVERT_I12_I16_BLOCK
        #undef CONVERT_I12_2I32_SEPARATED    
        #undef CONVERT_CI12_2CI32_BLOCK_OPT
        #undef CONVERT_CI12_4CI32_BLOCK_OPT
    }

    //AVX2 block
    {
        #include "conv_i12_i16_avx2.inc"
        #include "conv_ci12_4ci16_avx2.inc"

        if(i >= 96)
        {
            __m256i y0 = _mm256_maskload_epi64((const long long*)(in + 0), load_mask);   // 8 1/3
            __m256i y1 = _mm256_maskload_epi64((const long long*)(in + 3), load_mask);   // 8 1/3
            __m256i y2 = _mm256_maskload_epi64((const long long*)(in + 6), load_mask);   // 8 1/3
            __m256i y3 = _mm256_maskload_epi64((const long long*)(in + 9), load_mask);   // 8 1/3
            in += 12;
            i -= 96;

            STORE_CI12_4CI16_BLOCK(y0, y1, y2, y3);
        }

        #undef CONVERT_I12_I16_BLOCK
        #undef CONVERT_I12_2I32_SEPARATED    
        #undef CONVERT_CI12_2CI32_BLOCK_OPT
        #undef CONVERT_CI12_4CI32_BLOCK_OPT
    }

    //Generic block
    {
        const uint8_t *indata = (const uint8_t*)in;
        #include "conv_ci12_4ci16_generic.inc"
    }
}
#undef TEMPLATE_FUNC_NAME
