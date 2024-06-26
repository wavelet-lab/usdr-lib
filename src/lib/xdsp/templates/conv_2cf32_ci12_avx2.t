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
    const __m256i maske = _mm256_set1_epi64x(0x0000fff00000fff0);
    const __m256i masko = _mm256_set1_epi64x(0xfff00000fff00000);

    const __m256i shfl = _mm256_set_epi8(
        0x80, 0x80, 0x80, 0x80, 0x0f, 0x0e, 0x0d, 0x0b,
        0x0a, 0x09, 0x07, 0x06, 0x05, 0x03, 0x02, 0x01,
        0x80, 0x80, 0x80, 0x80, 0x0f, 0x0e, 0x0d, 0x0b,
        0x0a, 0x09, 0x07, 0x06, 0x05, 0x03, 0x02, 0x01);

    const __m256i permmask = _mm256_set_epi32(7,3,6,5,4,2,1,0);
    const __m256i storemask = _mm256_set_epi64x(0, -1, -1, -1);


#define CONVERT_F32_I12_BLOCK(v0, v1) \
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
        __m256i ro0 = _mm256_and_si256(ii0, masko); \
        __m256i re0 = _mm256_slli_epi64(_mm256_and_si256(ii0, maske), 4); \
        __m256i r0  = _mm256_or_si256(ro0, re0); \
    \
        __m256i res  = _mm256_shuffle_epi8(r0, shfl); \
        res  = _mm256_permutevar8x32_epi32(res, permmask); \
    \
        _mm256_maskstore_epi64((long long*)out64, storemask, res); \
        out64 += 3; \
    }
    // CONVERT_F32_I12_BLOCK end

    __m256  v0, v1, v2, v3;

    for (; i >= 32*4; i -= 32*4)
    {
        v0 = _mm256_loadu_ps(indata_0 + 0);
        v1 = _mm256_loadu_ps(indata_1 + 0);
        v2 = _mm256_loadu_ps(indata_0 + 8);
        v3 = _mm256_loadu_ps(indata_1 + 8);
        indata_0 += 16;
        indata_1 += 16;

        CONVERT_F32_I12_BLOCK(v0, v1);
        CONVERT_F32_I12_BLOCK(v2, v3);
    }

    for (; i >= 32*2; i -= 32*2)
    {
        v0 = _mm256_loadu_ps(indata_0 + 0);
        v1 = _mm256_loadu_ps(indata_1 + 0);
        indata_0 += 8;
        indata_1 += 8;

        CONVERT_F32_I12_BLOCK(v0, v1);
    }

#undef CONVERT_F32_I12_BLOCK

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
