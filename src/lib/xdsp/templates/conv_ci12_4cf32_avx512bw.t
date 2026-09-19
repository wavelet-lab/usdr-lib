#ifndef UNWRAP_CNT
#define UNWRAP_CNT 4
#endif

#if UNWRAP_CNT > 4
#error Maximum spported UNWRAP_CNT is 4!
#endif


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
    /* 12 bits -> 32 bits  =>  3 -> 8   */
    if ((outdatabsz * 3 / 8) < i)
        i = (outdatabsz * 3 / 8);

    float* outdata_0 = (float*)outdata_0_p;
    float* outdata_1 = (float*)outdata_1_p;
    float* outdata_2 = (float*)outdata_2_p;
    float* outdata_3 = (float*)outdata_3_p;

    const uint64_t *in = (const uint64_t*)indata_p;

    //AVX512 block
    {
        #include "conv_i12_i16_avx512bw.inc"
        #include "conv_i12_f32_avx512bw.inc"

        __m512i y0, y1;
        __m512 f0, f1, f2, f3;
#if UNWRAP_CNT > 1
        __m512i y2, y3;
        __m512 f4, f5, f6, f7;
#if UNWRAP_CNT > 2
        __m512i y4, y5;
        __m512 f8, f9, fA, fB;
#if UNWRAP_CNT > 3
        __m512i y6, y7;
        __m512 fC, fD, fE, fF;
#endif
#endif
#endif

        for(; i >= 96 * UNWRAP_CNT; i -= 96 * UNWRAP_CNT)
        {
            y0 = _mm512_maskz_loadu_epi64(0b00111111, (const long long*)(in + 0));
            y1 = _mm512_maskz_loadu_epi64(0b00111111, (const long long*)(in + 6));
#if UNWRAP_CNT > 1
            y2 = _mm512_maskz_loadu_epi64(0b00111111, (const long long*)(in + 12));
            y3 = _mm512_maskz_loadu_epi64(0b00111111, (const long long*)(in + 18));
#if UNWRAP_CNT > 2
            y4 = _mm512_maskz_loadu_epi64(0b00111111, (const long long*)(in + 24));
            y5 = _mm512_maskz_loadu_epi64(0b00111111, (const long long*)(in + 30));
#if UNWRAP_CNT > 3
            y6 = _mm512_maskz_loadu_epi64(0b00111111, (const long long*)(in + 36));
            y7 = _mm512_maskz_loadu_epi64(0b00111111, (const long long*)(in + 42));
#endif
#endif
#endif

            in += UNWRAP_CNT * 12;

            CONVERT_CI12_4CF32_BLOCK_OPT(y0, y1, f0, f1, f2, f3);
#if UNWRAP_CNT > 1
            CONVERT_CI12_4CF32_BLOCK_OPT(y2, y3, f4, f5, f6, f7);
#if UNWRAP_CNT > 2
            CONVERT_CI12_4CF32_BLOCK_OPT(y4, y5, f8, f9, fA, fB);
#if UNWRAP_CNT > 3
            CONVERT_CI12_4CF32_BLOCK_OPT(y6, y7, fC, fD, fE, fF);
#endif
#endif
#endif

            _mm512_store_ps(outdata_0, f0);
            _mm512_store_ps(outdata_1, f1);
            _mm512_store_ps(outdata_2, f2);
            _mm512_store_ps(outdata_3, f3);

            outdata_0 += 16;
            outdata_1 += 16;
            outdata_2 += 16;
            outdata_3 += 16;

#if UNWRAP_CNT > 1
            _mm512_store_ps(outdata_0, f4);
            _mm512_store_ps(outdata_1, f5);
            _mm512_store_ps(outdata_2, f6);
            _mm512_store_ps(outdata_3, f7);

            outdata_0 += 16;
            outdata_1 += 16;
            outdata_2 += 16;
            outdata_3 += 16;

#if UNWRAP_CNT > 2
            _mm512_store_ps(outdata_0, f8);
            _mm512_store_ps(outdata_1, f9);
            _mm512_store_ps(outdata_2, fA);
            _mm512_store_ps(outdata_3, fB);

            outdata_0 += 16;
            outdata_1 += 16;
            outdata_2 += 16;
            outdata_3 += 16;
#if UNWRAP_CNT > 3
            _mm512_store_ps(outdata_0, fC);
            _mm512_store_ps(outdata_1, fD);
            _mm512_store_ps(outdata_2, fE);
            _mm512_store_ps(outdata_3, fF);

            outdata_0 += 16;
            outdata_1 += 16;
            outdata_2 += 16;
            outdata_3 += 16;
#endif
#endif
#endif
        }

        #undef CONVERT_I12_I16_BLOCK
        #undef CONVERT_I12_2I32_SEPARATED    
        #undef CONVERT_CI12_2CI32_BLOCK_OPT
        #undef CONVERT_CI12_4CI32_BLOCK_OPT
        
        #undef CONVERT_I12_F32_BLOCK
        #undef CONVERT_I12_F32_BLOCK_STORE1
        #undef CONVERT_CI12_2CF32_BLOCK_OPT
        #undef CONVERT_CI12_4CF32_BLOCK_OPT
    }


    //AVX2 block
    {
        #include "conv_i12_i16_avx2.inc"
        #include "conv_i12_f32_avx2.inc"

        if(i >= 48)
        {
            __m256i y0 = _mm256_maskload_epi64((const long long*)(in + 0), load_mask);
            __m256i y1 = _mm256_maskload_epi64((const long long*)(in + 3), load_mask);

            __m256 f0, f1, f2, f3;

            CONVERT_CI12_4CF32_BLOCK_OPT(y0, y1, f0, f1, f2, f3);

            _mm256_store_ps(outdata_0 + 0, f0);
            _mm256_store_ps(outdata_1 + 0, f1);
            _mm256_store_ps(outdata_2 + 0, f2);
            _mm256_store_ps(outdata_3 + 0, f3);

            outdata_0 += 8;
            outdata_1 += 8;
            outdata_2 += 8;
            outdata_3 += 8;

            in += 6;
            i -= 48;
        }
        
        #undef CONVERT_I12_I16_BLOCK
        #undef CONVERT_I12_2I32_SEPARATED    
        #undef CONVERT_CI12_2CI32_BLOCK_OPT
        #undef CONVERT_CI12_4CI32_BLOCK_OPT
        
        #undef CONVERT_I12_F32_BLOCK
        #undef CONVERT_I12_F32_BLOCK_STORE1
        #undef CONVERT_CI12_2CF32_BLOCK_OPT
        #undef CONVERT_CI12_4CF32_BLOCK_OPT
    }

    //Generic block
    {
        const uint8_t *indata = (const uint8_t*)in;
        #include "conv_ci12_4cf32_generic.inc"
    }
}

#undef TEMPLATE_FUNC_NAME
