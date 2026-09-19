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
    if ((outdatabsz) < i)
        i = (outdatabsz);

    uint32_t* outdata_0 = (uint32_t*)outdata_0_p;
    uint32_t* outdata_1 = (uint32_t*)outdata_1_p;
    uint32_t* outdata_2 = (uint32_t*)outdata_2_p;
    uint32_t* outdata_3 = (uint32_t*)outdata_3_p;
    uint32_t* outdata_4 = (uint32_t*)outdata_4_p;
    uint32_t* outdata_5 = (uint32_t*)outdata_5_p;

    const __m256i loadmask = _mm256_set_epi64x(0, -1, -1, -1);
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

        _mm_store_si128((__m128i*)outdata_0, _mm256_castsi256_si128(c0));
        _mm_store_si128((__m128i*)outdata_1, _mm256_castsi256_si128(c1));
        _mm_store_si128((__m128i*)outdata_2, _mm256_castsi256_si128(c2));
        _mm_store_si128((__m128i*)outdata_3, _mm256_castsi256_si128(c3));
        _mm_store_si128((__m128i*)outdata_4, _mm256_extracti128_si256(c0, 1));
        _mm_store_si128((__m128i*)outdata_5, _mm256_extracti128_si256(c1, 1));

        outdata_0 += 4;
        outdata_1 += 4;
        outdata_2 += 4;
        outdata_3 += 4;
        outdata_4 += 4;
        outdata_5 += 4;

        i -= 12 * sizeof(uint64_t);
        inptr64 += 12;
    }

    //Generic block
    {
        const uint32_t* indata = (uint32_t*)inptr64;
        #include "conv_ci16_6ci16_generic.inc"
    }
}

#undef TEMPLATE_FUNC_NAME
