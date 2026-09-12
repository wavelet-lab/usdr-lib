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

#include "conv_i16_i12_avx2.inc"
#include "conv_f32_i12_avx2.inc"

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

    if (i >= 32*2)
    {
        v0 = _mm256_loadu_ps(indata +  0);
        v1 = _mm256_loadu_ps(indata +  8);
        indata += 16;
        i -= 32*2;

        CONVERT_F32_I12_BLOCK(v0, v1);
    }

#undef CONVERT_F32_I12_BLOCK
#undef CONVERT_I16_I12_BLOCK

#undef I16RND
#define I16RND(x) x > 0 ? (int16_t)(x + 0.5f) : (int16_t)(x - 0.5f)

    uint8_t* outdata = (uint8_t*)out64;
    #include "conv_f32_i12_generic.inc"
}

#undef TEMPLATE_FUNC_NAME
