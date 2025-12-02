static
void TEMPLATE_FUNC_NAME(const void *__restrict indata_0_p,
                        const void *__restrict indata_1_p,
                        unsigned indatabsz,
                        void *__restrict outdata_p,
                        unsigned outdatabsz)
{
    unsigned i = indatabsz;
    if ((outdatabsz * 4 / 3) < i)
        i = (outdatabsz * 4 / 3);

    const int16_t* indata_0 = (const int16_t*)indata_0_p;
    const int16_t* indata_1 = (const int16_t*)indata_1_p;
    uint64_t *out64 = (uint64_t*)outdata_p;

#include "conv_i16_i12_avx2.inc"
#include "conv_2ci16_ci12_avx2.inc"

    __m256i  v0, v1, v2, v3;

    for (; i >= 32*4; i -= 32*4)
    {
        v0 = _mm256_loadu_si256((__m256i*)(indata_0 +  0));
        v1 = _mm256_loadu_si256((__m256i*)(indata_1 +  0));
        v2 = _mm256_loadu_si256((__m256i*)(indata_0 + 16));
        v3 = _mm256_loadu_si256((__m256i*)(indata_1 + 16));
        indata_0 += 32;
        indata_1 += 32;

        STORE_2CI16_CI12_BLOCK(v0, v1);
        STORE_2CI16_CI12_BLOCK(v2, v3);
    }

    for (; i >= 32*2; i -= 32*2)
    {
        v0 = _mm256_loadu_si256((__m256i*)(indata_0 + 0));
        v1 = _mm256_loadu_si256((__m256i*)(indata_1 + 0));
        indata_0 += 16;
        indata_1 += 16;

        STORE_2CI16_CI12_BLOCK(v0, v1);
    }

#undef STORE_2CI16_CI12_BLOCK
#undef CONVERT_I16_I12_BLOCK

    uint8_t* outdata = (uint8_t*)out64;

#include "conv_2ci16_ci12_generic.inc"

}
#undef TEMPLATE_FUNC_NAME
