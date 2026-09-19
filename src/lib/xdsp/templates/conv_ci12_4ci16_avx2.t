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

#include "conv_i12_i16_avx2.inc"
#include "conv_ci12_4ci16_avx2.inc"

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
            STORE_CI12_4CI16_BLOCK(y0, y1, y2, y3);

            y0 = _mm256_maskload_epi64((const long long*)(in + 0), load_mask);   // 8 1/3
            y1 = _mm256_maskload_epi64((const long long*)(in + 3), load_mask);   // 8 1/3
            y2 = _mm256_maskload_epi64((const long long*)(in + 6), load_mask);   // 8 1/3
            y3 = _mm256_maskload_epi64((const long long*)(in + 9), load_mask);   // 8 1/3
            in += 12;
        }

        i -= 96;

        STORE_CI12_4CI16_BLOCK(y0, y1, y2, y3);
    }

#undef STORE_CI12_4CI16_BLOCK
#undef CONVERT_I12_I16_BLOCK

    const uint8_t *indata = (const uint8_t*)in;
    #include "conv_ci12_4ci16_generic.inc"
}
#undef TEMPLATE_FUNC_NAME
