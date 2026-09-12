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

    #include "conv_i12_i16_avx2.inc"
    #include "conv_i12_f32_avx2.inc"

    while(i >= 96)
    {
        __m256i y0 = _mm256_maskload_epi64((const long long*)(in + 0), load_mask);
        __m256i y1 = _mm256_maskload_epi64((const long long*)(in + 3), load_mask);
        __m256i y2 = _mm256_maskload_epi64((const long long*)(in + 6), load_mask);
        __m256i y3 = _mm256_maskload_epi64((const long long*)(in + 9), load_mask);

        __m256 f0, f1, f2, f3, f4, f5, f6, f7;

        CONVERT_CI12_4CF32_BLOCK_OPT(y0, y1, f0, f1, f2, f3);
        _mm256_store_ps(outdata_0 + 0, f0);
        _mm256_store_ps(outdata_1 + 0, f1);
        _mm256_store_ps(outdata_2 + 0, f2);
        _mm256_store_ps(outdata_3 + 0, f3);

        CONVERT_CI12_4CF32_BLOCK_OPT(y2, y3, f4, f5, f6, f7);
        _mm256_store_ps(outdata_0 + 8, f4);
        _mm256_store_ps(outdata_1 + 8, f5);
        _mm256_store_ps(outdata_2 + 8, f6);
        _mm256_store_ps(outdata_3 + 8, f7);

        outdata_0 += 16;
        outdata_1 += 16;
        outdata_2 += 16;
        outdata_3 += 16;

        in += 12;
        i -= 96;
    }

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

    const uint8_t *indata = (const uint8_t*)in;
    #include "conv_ci12_4cf32_generic.inc"
}

#undef TEMPLATE_FUNC_NAME
