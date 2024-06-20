static
void TEMPLATE_FUNC_NAME(const int16_t *__restrict indata,
                        unsigned indatabsz,
                        float *__restrict outa,
                        float *__restrict outb,
                        unsigned outdatabsz)
{
    size_t i = indatabsz;
    if ((outdatabsz / 2) < i) {
        i = (outdatabsz / 2);
    }

    const __m256i* vp = (const __m256i* )indata;
    float* outdata_0 = (float*)outa;
    float* outdata_1 = (float*)outb;

    const __m256  scale = _mm256_set1_ps(CONV_SCALE);
    const __m256i permmask = _mm256_set_epi32(7, 5, 3, 1, 6, 4, 2, 0);

/*
*  reg:
*  |              (3)              |              (2)              |              (1)              |              (0)              |
*  +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
*  | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 |
*  +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
*  |  f15  |  f14  |  f13  |  f12  |  f11  |  f10  |  f9   |  f8   |  f7   |  f6   |  f5   |  f4   |  f3   |  f2   |  f1   |  f0   |
*
*  _mm256_permutevar8x32_epi32:
*  |               |               |               |               |               |               |               |               |
*  |  f15  |  f14  |  f11  |  f10  |  f7   |  f6   |  f3   |  f2   |  f13  |  f12  |  f9   |  f8   |  f5   |  f4   |  f1   |  f0   |
*/

#define CONVERT_CI16_2F32_BLOCK(reg) \
    {   \
        reg = _mm256_permutevar8x32_epi32(reg, permmask); \
        \
        __m256i d0 = _mm256_cvtepi16_epi32(_mm256_castsi256_si128(reg)); \
        __m256i d1 = _mm256_cvtepi16_epi32(_mm256_extracti128_si256(reg, 1)); \
        \
        __m256 f0 = _mm256_cvtepi32_ps(d0);                                      /* 4 1|2 */ \
        __m256 f1 = _mm256_cvtepi32_ps(d1);                                      /* 4 1|2 */ \
        \
        f0 = _mm256_mul_ps(f0, scale);                                           /* 4 1|2 */ \
        f1 = _mm256_mul_ps(f1, scale);                                           /* 4 1|2 */ \
        \
        _mm256_storeu_ps(outdata_0, f0); outdata_0 += 8;                         /* 1 1|2 */ \
        _mm256_storeu_ps(outdata_1, f1); outdata_1 += 8;                         /* 1 1|2 */ \
    }
// CONVERT_CI16_2F32_BLOCK end

    __m256i t0, t1, t2;

    for(; i >= 96; i -= 96)
    {
        t0 = _mm256_loadu_si256(vp++);
        t1 = _mm256_loadu_si256(vp++);
        t2 = _mm256_loadu_si256(vp++);

        CONVERT_CI16_2F32_BLOCK(t0);
        CONVERT_CI16_2F32_BLOCK(t1);
        CONVERT_CI16_2F32_BLOCK(t2);
    }

    for(; i >= 64; i -= 64)
    {
        t0 = _mm256_loadu_si256(vp++);
        t1 = _mm256_loadu_si256(vp++);

        CONVERT_CI16_2F32_BLOCK(t0);
        CONVERT_CI16_2F32_BLOCK(t1);
    }

    for(; i >= 32; i -= 32)
    {
        t0 = _mm256_loadu_si256(vp++);
        CONVERT_CI16_2F32_BLOCK(t0);
    }

    const uint64_t *ld = (const uint64_t *)vp;

    for (; i >= 8; i -= 8) {
        uint64_t v = *(ld++);
        float a = (int16_t)(v);
        float b = (int16_t)(v>>16);
        float c = (int16_t)(v>>32);
        float d = (int16_t)(v>>48);

        *(outdata_0++) = a * CONV_SCALE;
        *(outdata_0++) = b * CONV_SCALE;
        *(outdata_1++) = c * CONV_SCALE;
        *(outdata_1++) = d * CONV_SCALE;
    }
}

#undef TEMPLATE_FUNC_NAME
