static
void TEMPLATE_FUNC_NAME(const void *__restrict indata,
                        unsigned indatabsz,
                        void *__restrict outdata_0_p,
                        void *__restrict outdata_1_p,
                        void *__restrict outdata_2_p,
                        unsigned outdatabsz)
{
    unsigned i = indatabsz;
    if ((outdatabsz / 2) < i)
        i = (outdatabsz / 2);

    float* outdata_0 = (float*)outdata_0_p;
    float* outdata_1 = (float*)outdata_1_p;
    float* outdata_2 = (float*)outdata_2_p;

    const __m256    scale = _mm256_set1_ps(CONV_SCALE);
    const __m256i * inptr = (const __m256i*)indata;

/*
  r0:  DCBA                          DCBA                       HGBA                     HGBA                         JGDA
  r1:  HGFE -> permute2x128_si256 -> FEHG permute2x128_si256 -> JIDC -> shuffle_epi32 -> IJCD -> _mm256_shuffle_pd -> KHEB
  r2:  LKJI                          LKJI                       LKFE                     LKJI                         LIFC
*/

#define CONV_3CF32(r0, r1, r2) \
{ \
            r1 = _mm256_permute2x128_si256(r1, r1, 0x1); \
    \
    __m256i x0 = _mm256_permute2x128_si256(r0, r1, 0b00100000); \
    __m256i x1 = _mm256_permute2x128_si256(r0, r1, 0b00110001); \
    __m256i x2 = r2; \
    \
    __m256i xx0 = x0; \
    __m256i xx1 = _mm256_permute2x128_si256(x1, x2, 0b00100000); \
    __m256i xx2 = _mm256_permute2x128_si256(x1, x2, 0b00110001); \
    \
            xx1 = _mm256_shuffle_epi32(xx1, _MM_SHUFFLE(1,0,3,2)); \
    \
    __m256i z0 = _mm256_castpd_si256(_mm256_shuffle_pd(_mm256_castsi256_pd(xx0), _mm256_castsi256_pd(xx1), 0b0000)); \
    __m256i z1 = _mm256_castpd_si256(_mm256_shuffle_pd(_mm256_castsi256_pd(xx0), _mm256_castsi256_pd(xx1), 0b1111)); \
    __m256i z2 = xx2; \
    \
    __m256i zz0 = z0; \
    __m256i zz1 = _mm256_castpd_si256(_mm256_shuffle_pd(_mm256_castsi256_pd(z1), _mm256_castsi256_pd(z2), 0b0000)); \
    __m256i zz2 = _mm256_castpd_si256(_mm256_shuffle_pd(_mm256_castsi256_pd(z1), _mm256_castsi256_pd(z2), 0b1111)); \
    \
    _mm256_store_ps(outdata_0, _mm256_mul_ps(_mm256_cvtepi32_ps(zz0), scale)); \
    _mm256_store_ps(outdata_1, _mm256_mul_ps(_mm256_cvtepi32_ps(zz1), scale)); \
    _mm256_store_ps(outdata_2, _mm256_mul_ps(_mm256_cvtepi32_ps(zz2), scale)); \
    \
    outdata_0 += 8; \
    outdata_1 += 8; \
    outdata_2 += 8; \
}

    for (; i >= 32 * 3; i -= 32 * 3)
    {
        __m256i a0 = _mm256_loadu_si256(inptr++);
        __m256i a1 = _mm256_loadu_si256(inptr++);
        __m256i a2 = _mm256_loadu_si256(inptr++);

        __m256i b0 = _mm256_cvtepi16_epi32(_mm256_castsi256_si128(a0));
        __m256i b1 = _mm256_cvtepi16_epi32(_mm256_extracti128_si256(a0, 1));
        __m256i b2 = _mm256_cvtepi16_epi32(_mm256_castsi256_si128(a1));
        __m256i b3 = _mm256_cvtepi16_epi32(_mm256_extracti128_si256(a1,1));
        __m256i b4 = _mm256_cvtepi16_epi32(_mm256_castsi256_si128(a2));
        __m256i b5 = _mm256_cvtepi16_epi32(_mm256_extracti128_si256(a2, 1));

        CONV_3CF32(b0, b1, b2);
        CONV_3CF32(b3, b4, b5);
    }

#undef CONV_3CF32

    const uint32_t *ld = (const uint32_t *)inptr;
    #include "conv_ci16_3cf32_generic.inc"
}

#undef TEMPLATE_FUNC_NAME
