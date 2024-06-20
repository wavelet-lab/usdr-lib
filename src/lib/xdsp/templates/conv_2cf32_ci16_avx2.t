static
void TEMPLATE_FUNC_NAME(const void *__restrict indata_0_p,
                        const void *__restrict indata_1_p,
                        unsigned indatabsz,
                        void *__restrict outdata_p,
                        unsigned outdatabsz)
{
    unsigned i = indatabsz;
    if ((outdatabsz * 2) < i)
        i = (outdatabsz * 2);

    const float* indata_0 = (const float*)indata_0_p;
    const float* indata_1 = (const float*)indata_1_p;
    int16_t* outdata = (int16_t*)outdata_p;

    const __m256  scale = _mm256_set1_ps(1.0f / CONV_SCALE);

/*
*  |              (3)              |              (2)              |              (1)              |              (0)              |
*  +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
*  | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 |
*  +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
*  v0:
*  |  f13          |  f12          |  f9           |  f8           |  f5           |  f4           |  f1           |  f0           |
*  v1:
*  |  f15          |  f14          |  f11          |  f10          |  f7           |  f6           |  f3           |  f2           |
*
*  _mm256_packs_epi32:
*  |               |               |               |               |               |               |               |               |
*  |  f15  |  f14  |  f11  |  f10  |  f13  |  f12  |  f9   |  f8   |  f7   |  f6   |  f3   |  f2   |  f5   |  f4   |  f1   |  f0   |
*
*  res:
*  |  f15  |  f14  |  f13  |  f12  |  f11  |  f10  |  f9   |  f8   |  f7   |  f6   |  f5   |  f4   |  f3   |  f2   |  f1   |  f0   |
*/

#define CONVERT_2F32_CI12_BLOCK(v0, v1) \
    { \
        v0 = _mm256_mul_ps(v0, scale); \
        v1 = _mm256_mul_ps(v1, scale); \
    \
        __m256i i0 = _mm256_cvtps_epi32(v0); \
        __m256i i1 = _mm256_cvtps_epi32(v1); \
    \
        __m256i ii0 = _mm256_packs_epi32(i0, i1); \
        __m256i res = _mm256_shuffle_epi32(ii0, _MM_SHUFFLE(3,1,2,0)); \
    \
        _mm256_storeu_si256((__m256i *)outdata, res); \
        outdata += 16; \
    }
    // CONVERT_2F32_CI12_BLOCK end

    __m256  v0, v1, v2, v3;

    for (; i >= 32*4; i -= 32*4)
    {
        v0 = _mm256_loadu_ps(indata_0 + 0);
        v1 = _mm256_loadu_ps(indata_1 + 0);
        v2 = _mm256_loadu_ps(indata_0 + 8);
        v3 = _mm256_loadu_ps(indata_1 + 8);
        indata_0 += 16;
        indata_1 += 16;

        CONVERT_2F32_CI12_BLOCK(v0, v1);
        CONVERT_2F32_CI12_BLOCK(v2, v3);
    }

    for (; i >= 32*2; i -= 32*2)
    {
        v0 = _mm256_loadu_ps(indata_0 + 0);
        v1 = _mm256_loadu_ps(indata_1 + 0);
        indata_0 += 8;
        indata_1 += 8;

        CONVERT_2F32_CI12_BLOCK(v0, v1);
    }

#undef I16RND
#define I16RND(x) x > 0 ? (int16_t)(x + 0.5f) : (int16_t)(x - 0.5f)

    for (; i >= 16; i -= 16, indata_0 += 2, indata_1 += 2, outdata += 4)
    {
        float fa = indata_0[0] / CONV_SCALE;
        float fb = indata_0[1] / CONV_SCALE;
        float fc = indata_1[0] / CONV_SCALE;
        float fd = indata_1[1] / CONV_SCALE;

        int16_t a = I16RND(fa);
        int16_t b = I16RND(fb);
        int16_t c = I16RND(fc);
        int16_t d = I16RND(fd);

        uint64_t v = (uint64_t)(uint16_t)a | ((uint64_t)(uint16_t)b << 16) | ((uint64_t)(uint16_t)c << 32) | ((uint64_t)(uint16_t)d << 48);
        *(uint64_t*)outdata = v;
    }

    // do nothing with leftover
}

#undef TEMPLATE_FUNC_NAME
