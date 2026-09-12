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

    const __m256i load_mask = _mm256_set_epi64x(0, -1, -1, -1);

    const __m256i mask0 = _mm256_set1_epi64x(0xfff00000fff00000);
    const __m256i mask1 = _mm256_set1_epi64x(0x0000fff00000fff0);

    const __m256i permmask = _mm256_set_epi32(5, 4, 3, 7, 6, 2, 1, 0);
    const __m256i shfl = _mm256_set_epi8(
        0x0f, 0x0e, 0x0d, 0x80, 0x09, 0x08, 0x07, 0x80,
        0x0c, 0x0b, 0x0a, 0x80, 0x06, 0x05, 0x04, 0x80,
        0x0b, 0x0a, 0x09, 0x80, 0x05, 0x04, 0x03, 0x80,
        0x08, 0x07, 0x06, 0x80, 0x02, 0x01, 0x00, 0x80);

#define CONVERT_CI12_2CI16_BLOCK(reg, result) \
    {   \
        __m256i v0 = _mm256_permutevar8x32_epi32(reg, permmask); \
        __m256i r  = _mm256_shuffle_epi8(v0, shfl); \
                r  = _mm256_permute4x64_epi64(r, _MM_SHUFFLE(3, 1, 2, 0)); \
        \
        __m256i r0 = _mm256_and_si256(r, mask0); \
        __m256i r1 = _mm256_and_si256(_mm256_srli_epi64(r, 4), mask1); \
                result = _mm256_or_si256(r0, r1); \
    }

#define STORE_CI12_2CI16_BLOCK(reg0, reg1) \
    { \
        __m256i rs0, rs1; \
        CONVERT_CI12_2CI16_BLOCK(reg0, rs0); \
        CONVERT_CI12_2CI16_BLOCK(reg1, rs1); \
        _mm256_storeu_si256((__m256i*)outdata_0, _mm256_permute2x128_si256(rs0, rs1, 0b00100000)); \
        _mm256_storeu_si256((__m256i*)outdata_1, _mm256_permute2x128_si256(rs0, rs1, 0b00110001)); \
        outdata_0 += 16; \
        outdata_1 += 16; \
    }

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
            STORE_CI12_2CI16_BLOCK(y0, y1);
            STORE_CI12_2CI16_BLOCK(y2, y3);

            y0 = _mm256_maskload_epi64((const long long*)(in + 0), load_mask);   // 8 1/3
            y1 = _mm256_maskload_epi64((const long long*)(in + 3), load_mask);   // 8 1/3
            y2 = _mm256_maskload_epi64((const long long*)(in + 6), load_mask);   // 8 1/3
            y3 = _mm256_maskload_epi64((const long long*)(in + 9), load_mask);   // 8 1/3
            in += 12;
        }

        i -= 96;

        STORE_CI12_2CI16_BLOCK(y0, y1);
        STORE_CI12_2CI16_BLOCK(y2, y3);
    }

#undef STORE_CI12_2CI16_BLOCK
#undef CONVERT_CI12_2CI16_BLOCK

    const uint8_t *indata = (const uint8_t*)in;
    #include "conv_ci12_2ci16_generic.inc"
}
#undef TEMPLATE_FUNC_NAME
