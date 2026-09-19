static inline
void TEMPLATE_FUNC_NAME(const void *__restrict indata_p,
                        unsigned indatabsz,
                        void *__restrict outdata_0_p,
                        void *__restrict outdata_1_p,
                        unsigned outdatabsz)
{
    unsigned i = indatabsz;
    /* 12 bits -> 32 bits  =>  3 -> 8   */
    if ((outdatabsz * 3 / 8) < i)
        i = (outdatabsz * 3 / 8);

    const uint64_t *in = (const uint64_t*)indata_p;
    float* outdata_0 = (float*)outdata_0_p;
    float* outdata_1 = (float*)outdata_1_p;

    //AVX512 block
    {
        #include "conv_i12_i16_avx512bw.inc"
        #include "conv_i12_f32_avx512bw.inc"

        __m512i y0, y1;
        __m512 res0, res1, res2, res3;

        for(; i >= 96; i -= 96)
        {
            y0 = _mm512_maskz_loadu_epi64(0b00111111, (const long long*)(in + 0));
            y1 = _mm512_maskz_loadu_epi64(0b00111111, (const long long*)(in + 6));
            in += 12;
            
            CONVERT_CI12_2CF32_BLOCK_OPT(y0, res0, res1);
            _mm512_store_ps(outdata_0 + 0, res0);
            _mm512_store_ps(outdata_1 + 0, res1);
            
            CONVERT_CI12_2CF32_BLOCK_OPT(y1, res2, res3);
            _mm512_store_ps(outdata_0 + 16, res2);
            _mm512_store_ps(outdata_1 + 16, res3);
            
            outdata_0 += 32;
            outdata_1 += 32;
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
            in += 6;
            i -= 48;

            __m256 res0, res1, res2, res3;

            CONVERT_CI12_2CF32_BLOCK_OPT(y0, res0, res1);
            _mm256_store_ps(outdata_0 + 0, res0);
            _mm256_store_ps(outdata_1 + 0, res1);
            
            CONVERT_CI12_2CF32_BLOCK_OPT(y1, res2, res3);
            _mm256_store_ps(outdata_0 + 8, res2);
            _mm256_store_ps(outdata_1 + 8, res3);

            outdata_0 += 16;
            outdata_1 += 16;
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
        #include "conv_ci12_2cf32_generic.inc"
    }
}
#undef TEMPLATE_FUNC_NAME
