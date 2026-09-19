static
void TEMPLATE_FUNC_NAME(const void *__restrict indata_0_p,
                        const void *__restrict indata_1_p,
                        const void *__restrict indata_2_p,
                        unsigned indatabsz,
                        void *__restrict outdata_p,
                        unsigned outdatabsz)
{
    unsigned i = indatabsz;
    if ((outdatabsz * 2) < i)
        i = (outdatabsz * 2);

    const float* indata_0 = (const float*)indata_0_p;
    const float* indata_1 = (const float*)indata_1_p;
    const float* indata_2 = (const float*)indata_2_p;
    int16_t* outdata = (int16_t*)outdata_p;

    const __m256 scale = _mm256_set1_ps(1.0f / CONV_SCALE);

#define CONV_3CF32(r0, r1, r2) \
{ \
    __m256i z0 = _mm256_castpd_si256(_mm256_shuffle_pd(_mm256_castsi256_pd(r0), _mm256_castsi256_pd(r1), 0b0000)); \
    __m256i z1 = _mm256_castpd_si256(_mm256_shuffle_pd(_mm256_castsi256_pd(r0), _mm256_castsi256_pd(r1), 0b1111)); \
    __m256i z2 = r2; \
    \
    __m256i zz0 = z0; \
    __m256i zz1 = _mm256_castpd_si256(_mm256_shuffle_pd(_mm256_castsi256_pd(z1), _mm256_castsi256_pd(z2), 0b0000)); \
    __m256i zz2 = _mm256_castpd_si256(_mm256_shuffle_pd(_mm256_castsi256_pd(z1), _mm256_castsi256_pd(z2), 0b1111)); \
    \
    \
            zz1 = _mm256_shuffle_epi32(zz1, _MM_SHUFFLE(1,0,3,2)); \
    \
    \
    __m256i x0 = _mm256_permute2x128_si256(zz0, zz1, 0b00100000); \
    __m256i x1 = _mm256_permute2x128_si256(zz0, zz1, 0b00110001); \
    __m256i x2 = zz2; \
    \
            r0 = x0; \
            r1 = _mm256_permute2x128_si256(x1, x2, 0b00100000); \
            r2 = _mm256_permute2x128_si256(x1, x2, 0b00110001); \
    \
    \
            r1 = _mm256_permute2x128_si256(r1, r1, 0x1); \
    \
}

    for (; i >= 32 * 6; i -= 32 * 6)
    {
        __m256i a0 = _mm256_load_si256((const __m256i*)indata_0);
        __m256i a1 = _mm256_load_si256((const __m256i*)indata_1);
        __m256i a2 = _mm256_load_si256((const __m256i*)indata_2);
        __m256i a3 = _mm256_load_si256((const __m256i*)(indata_0 + 8));
        __m256i a4 = _mm256_load_si256((const __m256i*)(indata_1 + 8));
        __m256i a5 = _mm256_load_si256((const __m256i*)(indata_2 + 8));
        indata_0 += 16;
        indata_1 += 16;
        indata_2 += 16;

        CONV_3CF32(a0, a1, a2);
        CONV_3CF32(a3, a4, a5);

        __m256 p0 = _mm256_castsi256_ps(_mm256_permute2x128_si256(a0, a1, 0b00100000));
        __m256 p1 = _mm256_castsi256_ps(_mm256_permute2x128_si256(a0, a1, 0b00110001));
        __m256 p2 = _mm256_castsi256_ps(_mm256_permute2x128_si256(a2, a3, 0b00100000));
        __m256 p3 = _mm256_castsi256_ps(_mm256_permute2x128_si256(a2, a3, 0b00110001));
        __m256 p4 = _mm256_castsi256_ps(_mm256_permute2x128_si256(a4, a5, 0b00100000));
        __m256 p5 = _mm256_castsi256_ps(_mm256_permute2x128_si256(a4, a5, 0b00110001));

               p0 = _mm256_mul_ps(p0, scale);
               p1 = _mm256_mul_ps(p1, scale);
               p2 = _mm256_mul_ps(p2, scale);
               p3 = _mm256_mul_ps(p3, scale);
               p4 = _mm256_mul_ps(p4, scale);
               p5 = _mm256_mul_ps(p5, scale);

        __m256i res0 = _mm256_packs_epi32(_mm256_cvtps_epi32(p0), _mm256_cvtps_epi32(p1));
        __m256i res1 = _mm256_packs_epi32(_mm256_cvtps_epi32(p2), _mm256_cvtps_epi32(p3));
        __m256i res2 = _mm256_packs_epi32(_mm256_cvtps_epi32(p4), _mm256_cvtps_epi32(p5));

        _mm256_storeu_si256((__m256i*)(outdata +  0), res0);
        _mm256_storeu_si256((__m256i*)(outdata + 16), res1);
        _mm256_storeu_si256((__m256i*)(outdata + 32), res2);
        outdata += 48;
    }

#undef CONV_3CF32

    #include "conv_3cf32_ci16_generic.inc"
}

#undef TEMPLATE_FUNC_NAME
