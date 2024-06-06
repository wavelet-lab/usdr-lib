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

    const __m256i shl_ctrl = _mm256_set_epi64x(64,32,16,0);
    const __m256i shr_ctrl = _mm256_set_epi64x(0,16,32,48);
    const __m256  scale = _mm256_set1_ps(CONV_SCALE);
    const __m256i load_mask = _mm256_set_epi64x(0, -1, -1, -1);

    const __m256i mask0 = _mm256_set1_epi64x(0xfff00000fff00000);
    const __m256i mask1 = _mm256_set1_epi64x(0x0000fff00000fff0);
    const __m256i shfl = _mm256_set_epi8(
        0x0d, 0x0c, 0x0b, 0x80, 0x0a, 0x09, 0x08, 0x80,
        0x05, 0x04, 0x03, 0x80, 0x02, 0x01, 0x00, 0x80,
        0x0d, 0x0c, 0x0b, 0x80, 0x0a, 0x09, 0x08, 0x80,
        0x05, 0x04, 0x03, 0x80, 0x02, 0x01, 0x00, 0x80);

#define CONVERT_I12_F32_BLOCK(reg) \
    {   \
        __m256i v0 = _mm256_sllv_epi64(reg, shl_ctrl);                           /* 1 1|2 */ \
        __m256i v1 = _mm256_srlv_epi64(reg, shr_ctrl);                           /* 1 1|2 */ \
        __m256i v2 = _mm256_permute4x64_epi64(v1, _MM_SHUFFLE(2,1,0,3));         /* 3   1 */ \
        __m256i v3 = _mm256_or_si256(v0, v2);                                    /* 1 1|3 */ \
        \
        __m256i r  = _mm256_shuffle_epi8(v3, shfl); \
                r  = _mm256_shuffle_epi32(r, _MM_SHUFFLE(3, 1, 2, 0)); \
                r  = _mm256_permute4x64_epi64(r, _MM_SHUFFLE(3, 1, 2, 0)); \
        \
        __m256i r0 = _mm256_and_si256(r, mask0); \
        __m256i r1 = _mm256_and_si256(_mm256_srli_epi64(r, 4), mask1); \
        __m256i result = _mm256_or_si256(r0, r1); \
        \
        __m256i d0 = _mm256_cvtepi16_epi32(_mm256_castsi256_si128(result)); \
        __m256i d1 = _mm256_cvtepi16_epi32(_mm256_extracti128_si256(result, 1)); \
        \
        __m256 f0 = _mm256_cvtepi32_ps(d0);                                      /* 4 1|2 */ \
        __m256 f1 = _mm256_cvtepi32_ps(d1);                                      /* 4 1|2 */ \
        \
        f0 = _mm256_mul_ps(f0, scale);                                           /* 4 1|2 */ \
        f1 = _mm256_mul_ps(f1, scale);                                           /* 4 1|2 */ \
        \
        _MM256_STOREX_PS(outdata_0, f0); outdata_0 += 8;                         /* 1 1|2 */ \
        _MM256_STOREX_PS(outdata_1, f1); outdata_1 += 8;                         /* 1 1|2 */ \
    }
// CONVERT_I12_F32_BLOCK end                                                      lat  40

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
            CONVERT_I12_F32_BLOCK(y0);
            CONVERT_I12_F32_BLOCK(y1);
            CONVERT_I12_F32_BLOCK(y2);
            CONVERT_I12_F32_BLOCK(y3);

            y0 = _mm256_maskload_epi64((const long long*)(in + 0), load_mask);   // 8 1/3
            y1 = _mm256_maskload_epi64((const long long*)(in + 3), load_mask);   // 8 1/3
            y2 = _mm256_maskload_epi64((const long long*)(in + 6), load_mask);   // 8 1/3
            y3 = _mm256_maskload_epi64((const long long*)(in + 9), load_mask);   // 8 1/3
            in += 12;
        }

        i -= 96;

        CONVERT_I12_F32_BLOCK(y0);
        CONVERT_I12_F32_BLOCK(y1);
        CONVERT_I12_F32_BLOCK(y2);
        CONVERT_I12_F32_BLOCK(y3);
    }

#undef CONVERT_I12_F32_BLOCK

    const uint8_t *indata = (const uint8_t*)in;
    float **dest = &outdata_0;

    while(i >= 3)
    {
        uint8_t v0 = *(indata++);
        uint8_t v1 = *(indata++);
        uint8_t v2 = *(indata++);
        i -= 3;

        float a = (int16_t) (((uint16_t)v0 << 4) | ((uint16_t)v1 << 12));
        float b = (int16_t) (((uint16_t)v2 << 8) | (v1 & 0xf0));

        *((*dest)++) = a * CONV_SCALE;
        *((*dest)++) = b * CONV_SCALE;

        dest = (*dest == outdata_0) ? &outdata_1 : &outdata_0;
    }

    if(i >= 2)
    {
        uint16_t v = *(const uint16_t*)indata;
        float a = (int16_t)(v << 4);
        *((*dest)++) = a * CONV_SCALE;
        i -= 2;
    }
}
#undef TEMPLATE_FUNC_NAME
