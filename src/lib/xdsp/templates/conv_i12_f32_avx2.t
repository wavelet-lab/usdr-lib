static
void TEMPLATE_FUNC_NAME(const void *__restrict indata_p,
                        unsigned indatabsz,
                        void *__restrict outdata_p,
                        unsigned outdatabsz)
{
    unsigned i = indatabsz;
    /* 12 bits -> 32 bits  =>  3 -> 8   */
    if ((outdatabsz * 3 / 8) < i)
        i = (outdatabsz * 3 / 8);

    const uint64_t *in = (const uint64_t*)indata_p;
    float* outdata = (float*)outdata_p;

#include "conv_i12_i16_avx2.inc"
#include "conv_i12_f32_avx2.inc"

    __m256i y0, y1, y2, y3;

    if(i >= 96)
    {
        y0 = _mm256_maskload_epi64((const long long*)(in + 0), load_mask);   // 8 1/3
        y1 = _mm256_maskload_epi64((const long long*)(in + 3), load_mask);   // 8 1/3
        y2 = _mm256_maskload_epi64((const long long*)(in + 6), load_mask);   // 8 1/3
        y3 = _mm256_maskload_epi64((const long long*)(in + 9), load_mask);   // 8 1/3
        in += 12;

        for (; i >= 2*96; i -= 96)
        {
            CONVERT_I12_F32_BLOCK_STORE1(y0);
            CONVERT_I12_F32_BLOCK_STORE1(y1);
            CONVERT_I12_F32_BLOCK_STORE1(y2);
            CONVERT_I12_F32_BLOCK_STORE1(y3);

            y0 = _mm256_maskload_epi64((const long long*)(in + 0), load_mask);   // 8 1/3
            y1 = _mm256_maskload_epi64((const long long*)(in + 3), load_mask);   // 8 1/3
            y2 = _mm256_maskload_epi64((const long long*)(in + 6), load_mask);   // 8 1/3
            y3 = _mm256_maskload_epi64((const long long*)(in + 9), load_mask);   // 8 1/3
            in += 12;
        }

        i -= 96;

        CONVERT_I12_F32_BLOCK_STORE1(y0);
        CONVERT_I12_F32_BLOCK_STORE1(y1);
        CONVERT_I12_F32_BLOCK_STORE1(y2);
        CONVERT_I12_F32_BLOCK_STORE1(y3);
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
    #include "conv_i12_f32_generic.inc"
}
#undef TEMPLATE_FUNC_NAME
