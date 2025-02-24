static
void TEMPLATE_FUNC_NAME(const void *__restrict indata_0_p,
                        const void *__restrict indata_1_p,
                        unsigned indatabsz,
                        void *__restrict outdata_p,
                        unsigned outdatabsz)
{
    unsigned i = indatabsz;
    if ((outdatabsz * 8 / 3) < i)
        i = (outdatabsz * 8 / 3);

    const float* indata_0 = (const float*)indata_0_p;
    const float* indata_1 = (const float*)indata_1_p;
    uint64_t *out64 = (uint64_t*)outdata_p;

    const __m256  scale = _mm256_set1_ps(1.0f / CONV_SCALE);

#include "conv_i16_i12_avx2.inc"

#define CONVERT_2F32_I12_BLOCK(v0, v1) \
    { \
        v0 = _mm256_mul_ps(v0, scale); \
        v1 = _mm256_mul_ps(v1, scale); \
    \
        __m256i i0 = _mm256_cvtps_epi32(v0); \
        __m256i i1 = _mm256_cvtps_epi32(v1); \
    \
        __m256i ii0 = _mm256_packs_epi32(i0, i1); \
        ii0 = _mm256_shuffle_epi32(ii0, _MM_SHUFFLE(3,1,2,0)); \
    \
        CONVERT_I16_I12_BLOCK(ii0, out64); \
    }
// CONVERT_2F32_I12_BLOCK end

    __m256  v0, v1, v2, v3;

    for (; i >= 32*4; i -= 32*4)
    {
        v0 = _mm256_loadu_ps(indata_0 + 0);
        v1 = _mm256_loadu_ps(indata_1 + 0);
        v2 = _mm256_loadu_ps(indata_0 + 8);
        v3 = _mm256_loadu_ps(indata_1 + 8);
        indata_0 += 16;
        indata_1 += 16;

        CONVERT_2F32_I12_BLOCK(v0, v1);
        CONVERT_2F32_I12_BLOCK(v2, v3);
    }

    for (; i >= 32*2; i -= 32*2)
    {
        v0 = _mm256_loadu_ps(indata_0 + 0);
        v1 = _mm256_loadu_ps(indata_1 + 0);
        indata_0 += 8;
        indata_1 += 8;

        CONVERT_2F32_I12_BLOCK(v0, v1);
    }

#undef CONVERT_2F32_I12_BLOCK
#undef CONVERT_I16_I12_BLOCK

#undef I16RND
#define I16RND(x) x > 0 ? (int16_t)(x + 0.5f) : (int16_t)(x - 0.5f)

    uint8_t* outdata = (uint8_t*)out64;

    for (; i >= 16; i -= 16) {

        float f0 = *(indata_0++) / CONV_SCALE;
        float f1 = *(indata_0++) / CONV_SCALE;
        float f2 = *(indata_1++) / CONV_SCALE;
        float f3 = *(indata_1++) / CONV_SCALE;

        wu_i16u32_t a0 = {{I16RND(f0), I16RND(f1)}};
        wu_i16u32_t a1 = {{I16RND(f2), I16RND(f3)}};

        wu_u32b_t  c0 = {(a0.u & 0xfff00000) | ((a0.u << 4) & 0x000fff00)};
        wu_u32b_t  c1 = {(a1.u & 0xfff00000) | ((a1.u << 4) & 0x000fff00)};

        *(outdata++) = c0.b[1];
        *(outdata++) = c0.b[2];
        *(outdata++) = c0.b[3];

        *(outdata++) = c1.b[1];
        *(outdata++) = c1.b[2];
        *(outdata++) = c1.b[3];
    }
}
#undef TEMPLATE_FUNC_NAME
