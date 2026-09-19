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

#include "conv_i16_i12_avx2.inc"

    __m256i  v0, v1, v2, v3;

    for (; i >= 32*4; i -= 32*4)
    {
        v0 = _mm256_loadu_si256(in256 + 0);
        v1 = _mm256_loadu_si256(in256 + 1);
        v2 = _mm256_loadu_si256(in256 + 2);
        v3 = _mm256_loadu_si256(in256 + 3);
        in256 += 4;

        CONVERT_I16_I12_BLOCK(v0, out64);
        CONVERT_I16_I12_BLOCK(v1, out64);
        CONVERT_I16_I12_BLOCK(v2, out64);
        CONVERT_I16_I12_BLOCK(v3, out64);
    }

    for (; i >= 32*2; i -= 32*2)
    {
        v0 = _mm256_loadu_si256(in256 + 0);
        v1 = _mm256_loadu_si256(in256 + 1);
        in256 += 2;

        CONVERT_I16_I12_BLOCK(v0, out64);
        CONVERT_I16_I12_BLOCK(v1, out64);
    }

#undef CONVERT_I16_I12_BLOCK

    const int16_t* indata = (const int16_t*)in256;
    uint8_t* outdata = (uint8_t*)out64;

#include "conv_i16_i12_generic.inc"
}

#undef TEMPLATE_FUNC_NAME
