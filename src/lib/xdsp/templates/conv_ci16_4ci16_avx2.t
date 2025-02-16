static
void TEMPLATE_FUNC_NAME(const void *__restrict indata_p,
                        unsigned indatabsz,
                        void *__restrict outdata_0_p,
                        void *__restrict outdata_1_p,
                        void *__restrict outdata_2_p,
                        void *__restrict outdata_3_p,
                        unsigned outdatabsz)
{
    unsigned i = indatabsz;
    if ((outdatabsz) < i)
        i = (outdatabsz);

    const __m256i* vp = (const __m256i* )indata_p;

    uint32_t* outdata_0 = (uint32_t*)outdata_0_p;
    uint32_t* outdata_1 = (uint32_t*)outdata_1_p;
    uint32_t* outdata_2 = (uint32_t*)outdata_2_p;
    uint32_t* outdata_3 = (uint32_t*)outdata_3_p;

#define CI16_4CI16_CONV(a0, a1) \
    { \
        a0 = _mm256_permutevar8x32_epi32(a0, permmask); \
        a1 = _mm256_permutevar8x32_epi32(a1, permmask); \
        \
        __m256i b0 = _mm256_castpd_si256(_mm256_shuffle_pd(_mm256_castsi256_pd(a0), _mm256_castsi256_pd(a1), 0b0000)); \
        __m256i b1 = _mm256_castpd_si256(_mm256_shuffle_pd(_mm256_castsi256_pd(a0), _mm256_castsi256_pd(a1), 0b1111)); \
        \
        _mm_storeu_si128((__m128i*)outdata_0, _mm256_castsi256_si128(b0)); \
        _mm_storeu_si128((__m128i*)outdata_1, _mm256_extracti128_si256(b0, 1)); \
        _mm_storeu_si128((__m128i*)outdata_2, _mm256_castsi256_si128(b1)); \
        _mm_storeu_si128((__m128i*)outdata_3, _mm256_extracti128_si256(b1, 1)); \
        \
        outdata_0 += 4; \
        outdata_1 += 4; \
        outdata_2 += 4; \
        outdata_3 += 4; \
    }

    const __m256i permmask = _mm256_set_epi32(7, 3, 5, 1, 6, 2, 4, 0);
    __m256i r0, r1;

#if 0
    __m256i r2, r3;

    if(i >= 128)
    {
        r0 = _mm256_loadu_si256(vp + 0);
        r1 = _mm256_loadu_si256(vp + 1);
        r2 = _mm256_loadu_si256(vp + 2);
        r3 = _mm256_loadu_si256(vp + 3);
        vp += 4;

        while(i >= 2*128)
        {
            CI16_4CI16_CONV(r0, r1);
            CI16_4CI16_CONV(r2, r3);
            r0 = _mm256_loadu_si256(vp + 0);
            r1 = _mm256_loadu_si256(vp + 1);
            r2 = _mm256_loadu_si256(vp + 2);
            r3 = _mm256_loadu_si256(vp + 3);
            vp += 4;
            i -= 128;
        }
        i -= 128;
        CI16_4CI16_CONV(r0, r1);
        CI16_4CI16_CONV(r2, r3);
    }
#endif

    if(i >= 64)
    {
        r0 = _mm256_loadu_si256(vp + 0);
        r1 = _mm256_loadu_si256(vp + 1);
        vp += 2;

        while(i >= 2*64)
        {
            CI16_4CI16_CONV(r0, r1);
            r0 = _mm256_loadu_si256(vp + 0);
            r1 = _mm256_loadu_si256(vp + 1);
            vp += 2;
            i -= 64;
        }
        i -= 64;
        CI16_4CI16_CONV(r0, r1);
    }

    const uint32_t* indata = (uint32_t*)vp;

    for (; i >= 16; i -= 16)
    {
        *outdata_0++ = *indata++;
        *outdata_1++ = *indata++;
        *outdata_2++ = *indata++;
        *outdata_3++ = *indata++;
    }

    // do nothing with leftover
}

#undef TEMPLATE_FUNC_NAME
