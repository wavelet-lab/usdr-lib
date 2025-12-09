static
void TEMPLATE_FUNC_NAME(const void *__restrict indata_p,
                        unsigned indatabsz,
                        void *__restrict outdata_0_p,
                        void *__restrict outdata_1_p,
                        void *__restrict outdata_2_p,
                        void *__restrict outdata_3_p,
                        void *__restrict outdata_4_p,
                        void *__restrict outdata_5_p,
                        unsigned outdatabsz)
{
    unsigned i = indatabsz;
    if ((outdatabsz / 2) < i)
        i = (outdatabsz / 2);

    float* outdata_0 = (float*)outdata_0_p;
    float* outdata_1 = (float*)outdata_1_p;
    float* outdata_2 = (float*)outdata_2_p;
    float* outdata_3 = (float*)outdata_3_p;
    float* outdata_4 = (float*)outdata_4_p;
    float* outdata_5 = (float*)outdata_5_p;

    const __m256i loadmask = _mm256_set_epi64x(0, -1, -1, -1);
    const __m256  scale = _mm256_set1_ps(CONV_SCALE);

    const long long int* inptr64 = (long long int*)indata_p;

    while(i >= 12 * sizeof(uint64_t))
    {
        __m256i in0 = _mm256_maskload_epi64(inptr64 + 0, loadmask);
        __m256i in1 = _mm256_maskload_epi64(inptr64 + 3, loadmask);
        __m256i in2 = _mm256_maskload_epi64(inptr64 + 6, loadmask);
        __m256i in3 = _mm256_maskload_epi64(inptr64 + 9, loadmask);

        __m256i a0 = _mm256_unpacklo_epi32(in0, in1);
        __m256i a1 = _mm256_unpackhi_epi32(in0, in1);
        __m256i a2 = _mm256_unpacklo_epi32(in2, in3);
        __m256i a3 = _mm256_unpackhi_epi32(in2, in3);

        __m256d b0 = _mm256_shuffle_pd(_mm256_castsi256_pd(a0), _mm256_castsi256_pd(a1), 0b0000);
        __m256d b1 = _mm256_shuffle_pd(_mm256_castsi256_pd(a0), _mm256_castsi256_pd(a1), 0b1111);
        __m256d b2 = _mm256_shuffle_pd(_mm256_castsi256_pd(a2), _mm256_castsi256_pd(a3), 0b0000);
        __m256d b3 = _mm256_shuffle_pd(_mm256_castsi256_pd(a2), _mm256_castsi256_pd(a3), 0b1111);

        __m256i c0 = _mm256_castpd_si256(_mm256_shuffle_pd(b0, b2, 0b0000));
        __m256i c2 = _mm256_castpd_si256(_mm256_shuffle_pd(b0, b2, 0b1111));
        __m256i c1 = _mm256_castpd_si256(_mm256_shuffle_pd(b1, b3, 0b0000));
        __m256i c3 = _mm256_castpd_si256(_mm256_shuffle_pd(b1, b3, 0b1111));

        __m256i d0 = _mm256_cvtepi16_epi32(_mm256_castsi256_si128(c0));
        __m256i d1 = _mm256_cvtepi16_epi32(_mm256_castsi256_si128(c1));
        __m256i d2 = _mm256_cvtepi16_epi32(_mm256_castsi256_si128(c2));
        __m256i d3 = _mm256_cvtepi16_epi32(_mm256_castsi256_si128(c3));
        __m256i d4 = _mm256_cvtepi16_epi32(_mm256_extracti128_si256(c0, 1));
        __m256i d5 = _mm256_cvtepi16_epi32(_mm256_extracti128_si256(c1, 1));

        _mm256_store_ps(outdata_0, _mm256_mul_ps(_mm256_cvtepi32_ps(d0), scale));
        _mm256_store_ps(outdata_1, _mm256_mul_ps(_mm256_cvtepi32_ps(d1), scale));
        _mm256_store_ps(outdata_2, _mm256_mul_ps(_mm256_cvtepi32_ps(d2), scale));
        _mm256_store_ps(outdata_3, _mm256_mul_ps(_mm256_cvtepi32_ps(d3), scale));
        _mm256_store_ps(outdata_4, _mm256_mul_ps(_mm256_cvtepi32_ps(d4), scale));
        _mm256_store_ps(outdata_5, _mm256_mul_ps(_mm256_cvtepi32_ps(d5), scale));

        outdata_0 += 8;
        outdata_1 += 8;
        outdata_2 += 8;
        outdata_3 += 8;
        outdata_4 += 8;
        outdata_5 += 8;

        i -= 12 * sizeof(uint64_t);
        inptr64 += 12;
    }

    //Generic block
    {
        const int16_t *in = (const int16_t *)inptr64;
        #include "conv_ci16_6cf32_generic.inc"
    }
}

#undef TEMPLATE_FUNC_NAME
