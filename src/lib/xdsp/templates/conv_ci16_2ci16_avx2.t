static
void TEMPLATE_FUNC_NAME(const void *__restrict indata,
                        unsigned indatabsz,
                        void *__restrict outdata_0_p,
                        void *__restrict outdata_1_p,
                        unsigned outdatabsz)
{
    unsigned i = indatabsz;
    if ((outdatabsz) < i)
        i = (outdatabsz);

    const __m256i* vp = (const __m256i* )indata;
    int16_t* outdata_0 = (int16_t*)outdata_0_p;
    int16_t* outdata_1 = (int16_t*)outdata_1_p;

    const __m256i permmask = _mm256_set_epi32(7, 5, 3, 1, 6, 4, 2, 0);

/*
*  |              (3)              |              (2)              |              (1)              |              (0)              |
*  +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
*  | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 |
*  +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
*
*  reg0:
*  |  a15  |  a14  |  a13  |  a12  |  a11  |  a10  |  a9   |  a8   |  a7   |  a6   |  a5   |  a4   |  a3   |  a2   |  a1   |  a0   |
*  reg1:
*  |  a31  |  a30  |  a29  |  a28  |  a27  |  a26  |  a25  |  a24  |  a23  |  a22  |  a21  |  a20  |  a19  |  a18  |  a17  |  a16  |
*
*  _mm256_permutevar8x32_epi32:
*  |  a15  |  a14  |  a11  |  a10  |  a7   |  a6   |  a3   |  a2   |  a13  |  a12  |  a9   |  a8   |  a5   |  a4   |  a1   |  a0   |
*  |  a31  |  a30  |  a27  |  a26  |  a23  |  a22  |  a19  |  a18  |  a29  |  a28  |  a25  |  a24  |  a21  |  a20  |  a17  |  a16  |
*
*  outdata_0:
*  |  a29  |  a28  |  a25  |  a24  |  a21  |  a20  |  a17  |  a16  |  a13  |  a12  |  a9   |  a8   |  a5   |  a4   |  a1   |  a0   |
*  outdata_1:
*  |  a31  |  a30  |  a27  |  a26  |  a23  |  a22  |  a19  |  a18  |  a15  |  a14  |  a11  |  a10  |  a7   |  a6   |  a3   |  a2   |
*/

#define CONVERT_CI16_2CI16_BLOCK(reg0, reg1) \
    { \
        reg0 = _mm256_permutevar8x32_epi32(reg0, permmask); \
        reg1 = _mm256_permutevar8x32_epi32(reg1, permmask); \
        \
        __m256i r0 = _mm256_permute2x128_si256(reg0, reg1, 0b00100000); \
        __m256i r1 = _mm256_permute2x128_si256(reg0, reg1, 0b00110001); \
        \
        _mm256_storeu_si256((__m256i*)outdata_0, r0); \
        _mm256_storeu_si256((__m256i*)outdata_1, r1); \
        \
        outdata_0 += 16; \
        outdata_1 += 16; \
    }

    __m256i t0, t1, t2, t3;

    for(; i >= 128; i -= 128)
    {
        t0 = _mm256_loadu_si256(vp++);
        t1 = _mm256_loadu_si256(vp++);
        t2 = _mm256_loadu_si256(vp++);
        t3 = _mm256_loadu_si256(vp++);

        CONVERT_CI16_2CI16_BLOCK(t0, t1);
        CONVERT_CI16_2CI16_BLOCK(t2, t3);
    }

    for(; i >= 64; i -= 64)
    {
        t0 = _mm256_loadu_si256(vp++);
        t1 = _mm256_loadu_si256(vp++);

        CONVERT_CI16_2CI16_BLOCK(t0, t1);
    }

#undef CONVERT_CI16_2CI16_BLOCK

    const uint64_t *ld = (const uint64_t *)vp;

    for (; i >= 8; i -= 8) {
        uint64_t v = *(ld++);
        int16_t a = (int16_t)(v);
        int16_t b = (int16_t)(v>>16);
        int16_t c = (int16_t)(v>>32);
        int16_t d = (int16_t)(v>>48);

        *(outdata_0++) = a;
        *(outdata_0++) = b;
        *(outdata_1++) = c;
        *(outdata_1++) = d;
    }

    // do nothing with leftover
}

#undef TEMPLATE_FUNC_NAME
