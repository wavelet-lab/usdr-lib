static
void TEMPLATE_FUNC_NAME(const void *__restrict indata_p,
                        unsigned indatabsz,
                        void *__restrict outdata_p,
                        unsigned outdatabsz)
{
    unsigned i = indatabsz;
    /* 12 bits -> 16 bits  =>  3 -> 4   */
    if ((outdatabsz * 3 / 4) < i)
        i = (outdatabsz * 3 / 4);

    const uint64_t *in = (const uint64_t*)indata_p;
    int16_t* outdata = (int16_t*)outdata_p;

#include "conv_i12_i16_avx2.inc"

    __m256i y0, y1, y2, y3;
    __m256i rs0, rs1, rs2, rs3;

    if(i >= 96)
    {
        y0 = _mm256_maskload_epi64((const long long*)(in + 0), load_mask);   // 8 1/3
        y1 = _mm256_maskload_epi64((const long long*)(in + 3), load_mask);   // 8 1/3
        y2 = _mm256_maskload_epi64((const long long*)(in + 6), load_mask);   // 8 1/3
        y3 = _mm256_maskload_epi64((const long long*)(in + 9), load_mask);   // 8 1/3
        in += 12;

        for (; i >= 2*96; i -= 96)
        {
            CONVERT_I12_I16_BLOCK(y0, rs0);
            CONVERT_I12_I16_BLOCK(y1, rs1);
            CONVERT_I12_I16_BLOCK(y2, rs2);
            CONVERT_I12_I16_BLOCK(y3, rs3);

            _mm256_store_si256((__m256i*)(outdata +  0), rs0);
            _mm256_store_si256((__m256i*)(outdata + 16), rs1);
            _mm256_store_si256((__m256i*)(outdata + 32), rs2);
            _mm256_store_si256((__m256i*)(outdata + 48), rs3);
            outdata += 64;

            y0 = _mm256_maskload_epi64((const long long*)(in + 0), load_mask);   // 8 1/3
            y1 = _mm256_maskload_epi64((const long long*)(in + 3), load_mask);   // 8 1/3
            y2 = _mm256_maskload_epi64((const long long*)(in + 6), load_mask);   // 8 1/3
            y3 = _mm256_maskload_epi64((const long long*)(in + 9), load_mask);   // 8 1/3
            in += 12;
        }

        i -= 96;

        CONVERT_I12_I16_BLOCK(y0, rs0);
        CONVERT_I12_I16_BLOCK(y1, rs1);
        CONVERT_I12_I16_BLOCK(y2, rs2);
        CONVERT_I12_I16_BLOCK(y3, rs3);

        _mm256_store_si256((__m256i*)(outdata +  0), rs0);
        _mm256_store_si256((__m256i*)(outdata + 16), rs1);
        _mm256_store_si256((__m256i*)(outdata + 32), rs2);
        _mm256_store_si256((__m256i*)(outdata + 48), rs3);
        outdata += 64;
    }

#undef CONVERT_I12_I16_BLOCK
#undef CONVERT_I12_2I32_SEPARATED    
#undef CONVERT_CI12_2CI32_BLOCK_OPT
#undef CONVERT_CI12_4CI32_BLOCK_OPT

    const uint8_t* indata = (const uint8_t*)in;
    #include "conv_i12_i16_generic.inc"
}

#undef TEMPLATE_FUNC_NAME
