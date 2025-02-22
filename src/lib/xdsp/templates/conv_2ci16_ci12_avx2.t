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

#define STORE_2CI16_CI12_BLOCK(v0, v1) \
    { \
        __m256i i0 = _mm256_castpd_si256(_mm256_shuffle_pd(_mm256_castsi256_pd(v0), _mm256_castsi256_pd(v1), 0b0000)); \
        __m256i i1 = _mm256_castpd_si256(_mm256_shuffle_pd(_mm256_castsi256_pd(v0), _mm256_castsi256_pd(v1), 0b1111)); \
    \
        i0 = _mm256_shuffle_epi32(i0, _MM_SHUFFLE(3,1,2,0)); \
        i1 = _mm256_shuffle_epi32(i1, _MM_SHUFFLE(3,1,2,0)); \
    \
        __m256i z0 = _mm256_permute2x128_si256(i0, i1, 0b00100000); \
        __m256i z1 = _mm256_permute2x128_si256(i0, i1, 0b00110001); \
    \
        CONVERT_I16_I12_BLOCK(z0, out64); \
        CONVERT_I16_I12_BLOCK(z1, out64); \
    }
// STORE_2CI16_CI12_BLOCK end

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

    for (; i >= 8; i -= 8) {

        const int16_t i0 = *indata_0++;
        const int16_t q0 = *indata_0++;
        const int16_t i1 = *indata_1++;
        const int16_t q1 = *indata_1++;

        wu_i16u32_t a0 = {{i0, q0}};
        wu_i16u32_t a1 = {{i1, q1}};

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
