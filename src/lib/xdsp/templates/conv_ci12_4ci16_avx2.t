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

    const __m256i load_mask = _mm256_set_epi64x(0, -1, -1, -1);

    const __m256i mask0 = _mm256_set1_epi64x(0xfff00000fff00000);
    const __m256i mask1 = _mm256_set1_epi64x(0x0000fff00000fff0);

    const __m256i permmask0 = _mm256_set_epi32(5, 4, 3, 7, 6, 2, 1, 0);
    const __m256i permmask1 = _mm256_set_epi32(7, 3, 5, 1, 6, 2, 4, 0);

    const __m256i shfl = _mm256_set_epi8(
        0x0f, 0x0e, 0x0d, 0x80, 0x0c, 0x0b, 0x0a, 0x80,
        0x09, 0x08, 0x07, 0x80, 0x06, 0x05, 0x04, 0x80,
        0x0b, 0x0a, 0x09, 0x80, 0x08, 0x07, 0x06, 0x80,
        0x05, 0x04, 0x03, 0x80, 0x02, 0x01, 0x00, 0x80);

#define CONVERT_CI12_4CI16_BLOCK(reg, result) \
    {   \
        __m256i v0 = _mm256_permutevar8x32_epi32(reg, permmask0); \
        __m256i r  = _mm256_shuffle_epi8(v0, shfl); \
        \
        __m256i r0 = _mm256_and_si256(r, mask0); \
        __m256i r1 = _mm256_and_si256(_mm256_srli_epi64(r, 4), mask1); \
                result = _mm256_or_si256(r0, r1); \
    }

#define STORE_CI12_4CI16_BLOCK(reg0, reg1, reg2, reg3) \
    { \
        __m256i rs0, rs1, rs2, rs3; \
        CONVERT_CI12_4CI16_BLOCK(reg0, rs0); \
        CONVERT_CI12_4CI16_BLOCK(reg1, rs1); \
        CONVERT_CI12_4CI16_BLOCK(reg2, rs2); \
        CONVERT_CI12_4CI16_BLOCK(reg3, rs3); \
        \
        rs0 = _mm256_permutevar8x32_epi32(rs0, permmask1); \
        rs1 = _mm256_permutevar8x32_epi32(rs1, permmask1); \
        rs2 = _mm256_permutevar8x32_epi32(rs2, permmask1); \
        rs3 = _mm256_permutevar8x32_epi32(rs3, permmask1); \
        \
        __m256i z0 = _mm256_castpd_si256(_mm256_shuffle_pd(_mm256_castsi256_pd(rs0), _mm256_castsi256_pd(rs1), 0b0000)); \
        __m256i z1 = _mm256_castpd_si256(_mm256_shuffle_pd(_mm256_castsi256_pd(rs0), _mm256_castsi256_pd(rs1), 0b1111)); \
        __m256i z2 = _mm256_castpd_si256(_mm256_shuffle_pd(_mm256_castsi256_pd(rs2), _mm256_castsi256_pd(rs3), 0b0000)); \
        __m256i z3 = _mm256_castpd_si256(_mm256_shuffle_pd(_mm256_castsi256_pd(rs2), _mm256_castsi256_pd(rs3), 0b1111)); \
        \
        __m256i i0 = _mm256_permute2x128_si256(z0, z2, 0b00100000); \
        __m256i i1 = _mm256_permute2x128_si256(z0, z2, 0b00110001); \
        __m256i i2 = _mm256_permute2x128_si256(z1, z3, 0b00100000); \
        __m256i i3 = _mm256_permute2x128_si256(z1, z3, 0b00110001); \
        \
        _mm256_storeu_si256((__m256i*)outdata_0, i0); \
        _mm256_storeu_si256((__m256i*)outdata_1, i1); \
        _mm256_storeu_si256((__m256i*)outdata_2, i2); \
        _mm256_storeu_si256((__m256i*)outdata_3, i3); \
        \
        outdata_0 += 16; \
        outdata_1 += 16; \
        outdata_2 += 16; \
        outdata_3 += 16; \
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
#undef CONVERT_CI12_4CI16_BLOCK

    const uint8_t *indata = (const uint8_t*)in;

    for (; i >= 12; i -= 12) {
        /* read 12 bytes -> 4ci16 */

        uint64_t v0 = *(const uint64_t *)(indata + 0);
        uint64_t v1 = *(const uint64_t *)(indata + 6);
        indata += 12;

        *(outdata_0++) = (int16_t)((v0 <<  4)         );
        *(outdata_0++) = (int16_t)((v0 >>  8) & 0xfff0);
        *(outdata_1++) = (int16_t)((v0 >> 20) & 0xfff0);
        *(outdata_1++) = (int16_t)((v0 >> 32) & 0xfff0);
        *(outdata_2++) = (int16_t)((v1 <<  4)         );
        *(outdata_2++) = (int16_t)((v1 >>  8) & 0xfff0);
        *(outdata_3++) = (int16_t)((v1 >> 20) & 0xfff0);
        *(outdata_3++) = (int16_t)((v1 >> 32) & 0xfff0);
    }
    // do nothing with tail
}
#undef TEMPLATE_FUNC_NAME
