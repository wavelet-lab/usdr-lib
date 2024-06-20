static
void TEMPLATE_FUNC_NAME(const void *__restrict indata_0_p,
                        const void *__restrict indata_1_p,
                        unsigned indatabsz,
                        void *__restrict outdata_p,
                        unsigned outdatabsz)
{
    unsigned i = indatabsz;
    if ((outdatabsz) < i)
        i = (outdatabsz);


    int16_t* outdata = (int16_t*)outdata_p;
    const __m128i* vp0 = (const __m128i* )indata_0_p;
    const __m128i* vp1 = (const __m128i* )indata_1_p;

    const __m256i permmask = _mm256_set_epi32(7, 3, 6, 2, 5, 1, 4, 0);

/*
*  |              (3)              |              (2)              |              (1)              |              (0)              |
*  +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
*  | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 |
*  +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
*
*  reg:
*  |  a15  |  a14  |  a11  |  a10  |  a7   |  a6   |  a3   |  a2   |  a13  |  a12  |  a9   |  a8   |  a5   |  a4   |  a1   |  a0   |
*  |
*  |       7       |       6       |       5       |       4       |       3       |       2       |       1       |       0       |
*  _mm256_permutevar8x32_epi32:
*  |  a15  |  a14  |  a13  |  a12  |  a11  |  a10  |  a9   |  a8   |  a7   |  a6   |  a5   |  a4   |  a3   |  a2   |  a1   |  a0   |
*/

#define CONVERT_2CI16_CI12_BLOCK(reg) \
    { \
        reg = _mm256_permutevar8x32_epi32(reg, permmask); \
        _mm256_storeu_si256((__m256i*)outdata, reg); \
        outdata += 16; \
    }

    __m256i t0, t1, t2, t3;

    for(; i >= 128; i -= 128)
    {
        t0 = _mm256_loadu2_m128i(vp1++, vp0++);
        t1 = _mm256_loadu2_m128i(vp1++, vp0++);
        t2 = _mm256_loadu2_m128i(vp1++, vp0++);
        t3 = _mm256_loadu2_m128i(vp1++, vp0++);

        CONVERT_2CI16_CI12_BLOCK(t0);
        CONVERT_2CI16_CI12_BLOCK(t1);
        CONVERT_2CI16_CI12_BLOCK(t2);
        CONVERT_2CI16_CI12_BLOCK(t3);
    }

    for(; i >= 64; i -= 64)
    {
        t0 = _mm256_loadu2_m128i(vp1++, vp0++);
        t1 = _mm256_loadu2_m128i(vp1++, vp0++);

        CONVERT_2CI16_CI12_BLOCK(t0);
        CONVERT_2CI16_CI12_BLOCK(t1);
    }

    for(; i >= 32; i -= 32)
    {
        t0 = _mm256_loadu2_m128i(vp1++, vp0++);
        CONVERT_2CI16_CI12_BLOCK(t0);
    }

#undef CONVERT_2CI16_CI12_BLOCK

    const int16_t* indata_0 = (int16_t*)vp0;
    const int16_t* indata_1 = (int16_t*)vp1;

    for (; i >= 8; i -= 8, indata_0 += 2, indata_1 += 2, outdata += 4) {
        int16_t a = indata_0[0];
        int16_t b = indata_0[1];
        int16_t c = indata_1[0];
        int16_t d = indata_1[1];

        uint64_t v = (uint64_t)(uint16_t)a | ((uint64_t)(uint16_t)b << 16) | ((uint64_t)(uint16_t)c << 32) | ((uint64_t)(uint16_t)d << 48);
        *(uint64_t*)outdata = v;
    }

    // do nothing with leftover
}

#undef TEMPLATE_FUNC_NAME
