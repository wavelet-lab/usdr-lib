static
void TEMPLATE_FUNC_NAME(const void *__restrict indata_p,
                        unsigned indatabsz,
                        void *__restrict outdata_p,
                        unsigned outdatabsz)
{
    unsigned i = indatabsz;
    if ((outdatabsz * 2) < i)
        i = (outdatabsz * 2);

    const float* indata = (const float*)indata_p;
    int16_t* outdata = (int16_t*)outdata_p;
    const __m256  scale = _mm256_set1_ps(1.0f / CONV_SCALE);

#define CONVERT_F32_I16_BLOCK(v0, v1) \
    { \
        v0 = _mm256_mul_ps(v0, scale); \
        v1 = _mm256_mul_ps(v1, scale); \
    \
        __m256i i0 = _mm256_cvtps_epi32(v0); \
        __m256i i1 = _mm256_cvtps_epi32(v1); \
    \
        __m256i ii0 = _mm256_packs_epi32(i0, i1); \
        ii0 = _mm256_permute4x64_epi64(ii0, _MM_SHUFFLE(3,1,2,0)); \
    \
        _mm256_storeu_si256((__m256i *)outdata, ii0); \
        outdata += 16; \
    }
// CONVERT_F32_I16_BLOCK end

    __m256  v0, v1, v2, v3;

    for (; i >= 32*4; i -= 32*4)
    {
        v0 = _mm256_loadu_ps(indata +  0);
        v1 = _mm256_loadu_ps(indata +  8);
        v2 = _mm256_loadu_ps(indata + 16);
        v3 = _mm256_loadu_ps(indata + 24);
        indata += 32;

        CONVERT_F32_I16_BLOCK(v0, v1);
        CONVERT_F32_I16_BLOCK(v2, v3);
    }

    for (; i >= 32*2; i -= 32*2)
    {
        v0 = _mm256_loadu_ps(indata +  0);
        v1 = _mm256_loadu_ps(indata +  8);
        indata += 16;

        CONVERT_F32_I16_BLOCK(v0, v1);
    }

#undef CONVERT_F32_I16_BLOCK
#undef I16RND
#define I16RND(x) x > 0 ? (int16_t)(x + 0.5f) : (int16_t)(x - 0.5f)

    for (; i >= 16; i -= 16, indata += 4, outdata += 4)
    {
        float fa = indata[0] / CONV_SCALE;
        float fb = indata[1] / CONV_SCALE;
        float fc = indata[2] / CONV_SCALE;
        float fd = indata[3] / CONV_SCALE;

        int16_t a = I16RND(fa);
        int16_t b = I16RND(fb);
        int16_t c = I16RND(fc);
        int16_t d = I16RND(fd);

        uint64_t v = (uint64_t)(uint16_t)a | ((uint64_t)(uint16_t)b << 16) | ((uint64_t)(uint16_t)c << 32) | ((uint64_t)(uint16_t)d << 48);
        *(int64_t*)outdata = v;
    }

    for (; i >= 4; i -= 4)
    {
        float a = *(indata++) / CONV_SCALE;
        *(outdata++) = I16RND(a);
    }
}

#undef TEMPLATE_FUNC_NAME
