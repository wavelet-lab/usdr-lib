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

    const __m256  scale = _mm256_set1_ps(1.0f / CONV_SCALE);

#include "conv_i16_i12_avx2.inc"

    __m256  f0, f1, f2, f3;

    while (i >= 32*4)
    {
        f0 = _mm256_loadu_ps(indata_0);
        f1 = _mm256_loadu_ps(indata_1);
        f2 = _mm256_loadu_ps(indata_2);
        f3 = _mm256_loadu_ps(indata_3);

        __m256i i0 = _mm256_cvtps_epi32(_mm256_mul_ps(f0, scale));
        __m256i i1 = _mm256_cvtps_epi32(_mm256_mul_ps(f1, scale));
        __m256i i2 = _mm256_cvtps_epi32(_mm256_mul_ps(f2, scale));
        __m256i i3 = _mm256_cvtps_epi32(_mm256_mul_ps(f3, scale));

        /* 4-way CF32 deinterleave (see conv_4cf32_ci16_avx2.t */

        __m256i ii0 = _mm256_shuffle_epi32(_mm256_packs_epi32(i0, i1), _MM_SHUFFLE(3,1,2,0));
        __m256i ii1 = _mm256_shuffle_epi32(_mm256_packs_epi32(i2, i3), _MM_SHUFFLE(3,1,2,0));

        __m256i z0 = _mm256_castpd_si256(_mm256_shuffle_pd(_mm256_castsi256_pd(ii0), _mm256_castsi256_pd(ii1), 0b0000));
        __m256i z1 = _mm256_castpd_si256(_mm256_shuffle_pd(_mm256_castsi256_pd(ii0), _mm256_castsi256_pd(ii1), 0b1111));

        __m256i d0 = _mm256_permute2x128_si256(z0, z1, 0b00100000);
        __m256i d1 = _mm256_permute2x128_si256(z0, z1, 0b00110001);

        /* Convert linear data to CI12 */

        CONVERT_I16_I12_BLOCK(d0, out64);
        CONVERT_I16_I12_BLOCK(d1, out64);

        indata_0 += 8;
        indata_1 += 8;
        indata_2 += 8;
        indata_3 += 8;

        i -= 32*4;
    }

#undef CONVERT_I16_I12_BLOCK

#undef I16RND
#define I16RND(x) x > 0 ? (int16_t)(x + 0.5f) : (int16_t)(x - 0.5f)

    uint8_t* outdata = (uint8_t*)out64;

    for (; i >= 32; i -= 32) {

        float f0 = *(indata_0++) / CONV_SCALE;
        float f1 = *(indata_0++) / CONV_SCALE;
        float f2 = *(indata_1++) / CONV_SCALE;
        float f3 = *(indata_1++) / CONV_SCALE;
        float f4 = *(indata_2++) / CONV_SCALE;
        float f5 = *(indata_2++) / CONV_SCALE;
        float f6 = *(indata_3++) / CONV_SCALE;
        float f7 = *(indata_3++) / CONV_SCALE;

        wu_i16u32_t a0 = {{I16RND(f0), I16RND(f1)}};
        wu_i16u32_t a1 = {{I16RND(f2), I16RND(f3)}};
        wu_i16u32_t a2 = {{I16RND(f4), I16RND(f5)}};
        wu_i16u32_t a3 = {{I16RND(f6), I16RND(f7)}};

        wu_u32b_t  c0 = {(a0.u & 0xfff00000) | ((a0.u << 4) & 0x000fff00)};
        wu_u32b_t  c1 = {(a1.u & 0xfff00000) | ((a1.u << 4) & 0x000fff00)};
        wu_u32b_t  c2 = {(a2.u & 0xfff00000) | ((a2.u << 4) & 0x000fff00)};
        wu_u32b_t  c3 = {(a3.u & 0xfff00000) | ((a3.u << 4) & 0x000fff00)};

        const wu_u32b_t arr[] = {c0, c1, c2, c3};
        for(unsigned j = 0; j < 4; ++j)
        {
            *(outdata++) = arr[j].b[1];
            *(outdata++) = arr[j].b[2];
            *(outdata++) = arr[j].b[3];
        }
    }
}
#undef TEMPLATE_FUNC_NAME
