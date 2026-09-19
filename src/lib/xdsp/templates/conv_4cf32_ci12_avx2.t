static
void TEMPLATE_FUNC_NAME(const void *__restrict indata_0_p,
                        const void *__restrict indata_1_p,
                        const void *__restrict indata_2_p,
                        const void *__restrict indata_3_p,
                        unsigned indatabsz,
                        void *__restrict outdata_p,
                        unsigned outdatabsz)
{
    unsigned i = indatabsz;
    if ((outdatabsz * 8 / 3) < i)
        i = (outdatabsz * 8 / 3);

    const float* indata_0 = (const float*)indata_0_p;
    const float* indata_1 = (const float*)indata_1_p;
    const float* indata_2 = (const float*)indata_2_p;
    const float* indata_3 = (const float*)indata_3_p;

    uint64_t *out64 = (uint64_t*)outdata_p;

#include "conv_i16_i12_avx2.inc"
#include "conv_4cf32_ci12_avx2.inc"

    __m256  f0, f1, f2, f3;

    while (i >= 32*4)
    {
        f0 = _mm256_loadu_ps(indata_0);
        f1 = _mm256_loadu_ps(indata_1);
        f2 = _mm256_loadu_ps(indata_2);
        f3 = _mm256_loadu_ps(indata_3);

        CONVERT_4CF32_CI12_BLOCK(f0, f1, f2, f3);

        indata_0 += 8;
        indata_1 += 8;
        indata_2 += 8;
        indata_3 += 8;
        i -= 32*4;
    }

#undef CONVERT_4CF32_CI12_BLOCK
#undef CONVERT_I16_I12_BLOCK

#undef I16RND
#define I16RND(x) x > 0 ? (int16_t)(x + 0.5f) : (int16_t)(x - 0.5f)

    uint8_t* outdata = (uint8_t*)out64;
    #include "conv_4cf32_ci12_generic.inc"
}
#undef TEMPLATE_FUNC_NAME
