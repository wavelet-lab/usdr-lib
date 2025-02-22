static
void TEMPLATE_FUNC_NAME(const void *__restrict indata_p,
                        unsigned indatabsz,
                        void *__restrict outdata_p,
                        unsigned outdatabsz)
{
    unsigned i = indatabsz;
    if ((outdatabsz * 8 / 3) < i)
        i = (outdatabsz * 8 / 3);

    const float *indata = (const float*)indata_p;
    uint64_t *out64 = (uint64_t*)outdata_p;

    const __m256  scale = _mm256_set1_ps(1.0f / CONV_SCALE);

#include "conv_i16_i12_avx2.inc"

#define CONVERT_F32_I12_BLOCK(v0, v1) \
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
        CONVERT_I16_I12_BLOCK(ii0, out64); \
    }
// CONVERT_F32_I12_BLOCK end

    __m256  v0, v1, v2, v3;

    for (; i >= 32*4; i -= 32*4)
    {
        v0 = _mm256_loadu_ps(indata +  0);
        v1 = _mm256_loadu_ps(indata +  8);
        v2 = _mm256_loadu_ps(indata + 16);
        v3 = _mm256_loadu_ps(indata + 24);
        indata += 32;

        CONVERT_F32_I12_BLOCK(v0, v1);
        CONVERT_F32_I12_BLOCK(v2, v3);
    }

    for (; i >= 32*2; i -= 32*2)
    {
        v0 = _mm256_loadu_ps(indata +  0);
        v1 = _mm256_loadu_ps(indata +  8);
        indata += 16;

        CONVERT_F32_I12_BLOCK(v0, v1);
    }

#undef CONVERT_F32_I12_BLOCK
#undef CONVERT_I16_I12_BLOCK

#undef I16RND
#define I16RND(x) x > 0 ? (int16_t)(x + 0.5f) : (int16_t)(x - 0.5f)

    uint8_t* outdata = (uint8_t*)out64;

    for (; i >= 8; i -= 8) {

        float f0 = *(indata++) / CONV_SCALE;
        float f1 = *(indata++) / CONV_SCALE;

        wu_i16u32_t a = {{I16RND(f0), I16RND(f1)}};
        wu_u32b_t   c = {(a.u & 0xfff00000) | ((a.u << 4) & 0x000fff00)};

        *(outdata++) = c.b[1];
        *(outdata++) = c.b[2];
        *(outdata++) = c.b[3];
    }

    if(i >= 4)
    {
        float f = *indata / CONV_SCALE;
        wu_i16b_t c = {I16RND(f)};

        *(outdata++) = c.b[0];
        *(outdata++) = c.b[1] >> 4;
        i -= 4;
    }
}

#undef TEMPLATE_FUNC_NAME
