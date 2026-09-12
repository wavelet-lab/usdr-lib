static
void TEMPLATE_FUNC_NAME(const void *__restrict indata_0_p,
                        const void *__restrict indata_1_p,
                        unsigned indatabsz,
                        void *__restrict outdata_p,
                        unsigned outdatabsz)
{
    unsigned i = indatabsz;
    if ((outdatabsz * 8 / 3) < i)
        i = (outdatabsz * 8 / 3);

    const float* indata_0 = (const float*)indata_0_p;
    const float* indata_1 = (const float*)indata_1_p;
    uint64_t *out64 = (uint64_t*)outdata_p;

    //AVX512 block
    {
        #include "conv_i16_i12_avx512bw.inc"
        #include "conv_2cf32_ci12_avx512bw.inc"

        __m512 v0, v1;

        for (; i >= 64*2; i -= 64*2)
        {
            v0 = _mm512_loadu_ps(indata_0 + 0);
            v1 = _mm512_loadu_ps(indata_1 + 0);
            indata_0 += 16;
            indata_1 += 16;
            CONVERT_2F32_I12_BLOCK(v0, v1);
        }

        #undef CONVERT_2F32_I12_BLOCK
        #undef CONVERT_I16_I12_BLOCK
    }

    //AVX2 block
    {
        #include "conv_i16_i12_avx2.inc"
        #include "conv_2cf32_ci12_avx2.inc"

        if(i >= 32*2)
        {
            __m256 v0 = _mm256_loadu_ps(indata_0 + 0);
            __m256 v1 = _mm256_loadu_ps(indata_1 + 0);
            indata_0 += 8;
            indata_1 += 8;
            i -= 32*2;
            CONVERT_2F32_I12_BLOCK(v0, v1);
        }

        #undef CONVERT_2F32_I12_BLOCK
        #undef CONVERT_I16_I12_BLOCK
    }
    
    //Generic block
    {
        #undef I16RND
        #define I16RND(x) x > 0 ? (int16_t)(x + 0.5f) : (int16_t)(x - 0.5f)

        uint8_t* outdata = (uint8_t*)out64;
        #include "conv_2cf32_ci12_generic.inc"
    }
}
#undef TEMPLATE_FUNC_NAME
