static
void TEMPLATE_FUNC_NAME(const void *__restrict indata_p,
                        unsigned indatabsz,
                        void *__restrict outdata_p,
                        unsigned outdatabsz)
{
    unsigned i = indatabsz;
    if ((outdatabsz * 4 / 3) < i)
        i = (outdatabsz * 4 / 3);

    const __m256i *in256 = (__m256i*)indata_p;
    uint64_t *out64 = (uint64_t*)outdata_p;


    const __m256i maske = _mm256_set1_epi64x(0x0000fff00000fff0);
    const __m256i masko = _mm256_set1_epi64x(0xfff00000fff00000);

    const __m256i shfl = _mm256_set_epi8(
        0x80, 0x80, 0x80, 0x80, 0x0f, 0x0e, 0x0d, 0x0b,
        0x0a, 0x09, 0x07, 0x06, 0x05, 0x03, 0x02, 0x01,
        0x80, 0x80, 0x80, 0x80, 0x0f, 0x0e, 0x0d, 0x0b,
        0x0a, 0x09, 0x07, 0x06, 0x05, 0x03, 0x02, 0x01);

    const __m256i permmask = _mm256_set_epi32(7,3,6,5,4,2,1,0);
    const __m256i storemask = _mm256_set_epi64x(0, -1, -1, -1);

#define CONVERT_I16_I12_BLOCK(reg) \
{ \
    __m256i ro0 = _mm256_and_si256(reg, masko); \
    __m256i re0 = _mm256_slli_epi64(_mm256_and_si256(reg, maske), 4); \
    __m256i r0  = _mm256_or_si256(ro0, re0); \
\
    __m256i res  = _mm256_shuffle_epi8(r0, shfl); \
    res  = _mm256_permutevar8x32_epi32(res, permmask); \
\
    _mm256_maskstore_epi64((long long*)out64, storemask, res); \
    out64 += 3; \
}
// CONVERT_I16_I12_BLOCK end

    __m256i  v0, v1, v2, v3;

    for (; i >= 32*4; i -= 32*4)
    {
        v0 = _mm256_loadu_si256(in256 + 0);
        v1 = _mm256_loadu_si256(in256 + 1);
        v2 = _mm256_loadu_si256(in256 + 2);
        v3 = _mm256_loadu_si256(in256 + 3);
        in256 += 4;

        CONVERT_I16_I12_BLOCK(v0);
        CONVERT_I16_I12_BLOCK(v1);
        CONVERT_I16_I12_BLOCK(v2);
        CONVERT_I16_I12_BLOCK(v3);
    }

    for (; i >= 32*2; i -= 32*2)
    {
        v0 = _mm256_loadu_si256(in256 + 0);
        v1 = _mm256_loadu_si256(in256 + 1);
        in256 += 2;

        CONVERT_I16_I12_BLOCK(v0);
        CONVERT_I16_I12_BLOCK(v1);
    }

#undef CONVERT_F32_I12_BLOCK

    const int16_t* indata = (const int16_t*)in256;
    uint8_t* outdata = (uint8_t*)out64;

    for (; i >= 4; i -= 4) {

        const int16_t b0 = *indata++;
        const int16_t b1 = *indata++;

        wu_i16u32_t a = {{b0, b1}};
        wu_u32b_t   c = {(a.u & 0xfff00000) | ((a.u << 4) & 0x000fff00)};

        *(outdata++) = c.b[1];
        *(outdata++) = c.b[2];
        *(outdata++) = c.b[3];
    }

    if(i >= 2)
    {
        wu_i16b_t c = {*indata};

        *(outdata++) = c.b[0];
        *(outdata++) = c.b[1] >> 4;
        i -= 2;
    }
}

#undef TEMPLATE_FUNC_NAME
