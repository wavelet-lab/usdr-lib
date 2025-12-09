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

    #include "conv_i12_i16_avx2.inc"
    #include "conv_i12_f32_avx2.inc"

    while(i >= 96)
    {
        __m256i y0 = _mm256_maskload_epi64((const long long*)(in + 0), load_mask);
        __m256i y1 = _mm256_maskload_epi64((const long long*)(in + 3), load_mask);
        __m256i y2 = _mm256_maskload_epi64((const long long*)(in + 6), load_mask);
        __m256i y3 = _mm256_maskload_epi64((const long long*)(in + 9), load_mask);

        __m256 r0, r1;

        CONVERT_CI12_2CF32_BLOCK_OPT(y0, r0, r1);
        _mm256_store_ps(outdata_0 + 0, r0);
        _mm256_store_ps(outdata_1 + 0, r1);

        CONVERT_CI12_2CF32_BLOCK_OPT(y1, r0, r1);
        _mm256_store_ps(outdata_0 + 8, r0);
        _mm256_store_ps(outdata_1 + 8, r1);

        CONVERT_CI12_2CF32_BLOCK_OPT(y2, r0, r1);
        _mm256_store_ps(outdata_0 + 16, r0);
        _mm256_store_ps(outdata_1 + 16, r1);

        CONVERT_CI12_2CF32_BLOCK_OPT(y3, r0, r1);
        _mm256_store_ps(outdata_0 + 24, r0);
        _mm256_store_ps(outdata_1 + 24, r1);

        i -= 96;
        in += 12;
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

    const uint8_t *indata = (const uint8_t*)in;
    #include "conv_ci12_2cf32_generic.inc"
}
#undef TEMPLATE_FUNC_NAME
