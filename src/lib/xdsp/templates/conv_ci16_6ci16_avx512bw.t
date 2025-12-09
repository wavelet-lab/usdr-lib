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

    const long long int* inptr64 = (long long int*)indata_p;

    #include "conv_ci16_6ci16_avx512bw.inc"

    while(i >= 24 * sizeof(uint64_t))
    {
        __m512i in0 = _mm512_maskz_loadu_epi64(0b00111111, inptr64 +  0);
        __m512i in1 = _mm512_maskz_loadu_epi64(0b00111111, inptr64 +  6);
        __m512i in2 = _mm512_maskz_loadu_epi64(0b00111111, inptr64 + 12);
        __m512i in3 = _mm512_maskz_loadu_epi64(0b00111111, inptr64 + 18);
        
        __m512i c0, c1, c2, c3;
        AVX512_INTERLEAVE6(in0, in1, c0, c1);
        AVX512_INTERLEAVE6(in2, in3, c2, c3);

        __m512i d0 = _mm512_permutex2var_epi64(c0, permmask1, c2);
        __m512i d1 = _mm512_permutex2var_epi64(c1, permmask1, c3);
        __m512i d2 = _mm512_permutex2var_epi64(c0, permmask2, c2);
        __m512i d3 = _mm512_permutex2var_epi64(c1, permmask2, c3);

        _mm256_store_si256((__m256i*)outdata_0, _mm512_castsi512_si256(d0));
        _mm256_store_si256((__m256i*)outdata_1, _mm512_castsi512_si256(d1));
        _mm256_store_si256((__m256i*)outdata_2, _mm512_castsi512_si256(d2));
        _mm256_store_si256((__m256i*)outdata_3, _mm512_castsi512_si256(d3));
        _mm256_store_si256((__m256i*)outdata_4, _mm512_extracti64x4_epi64(d0, 1));
        _mm256_store_si256((__m256i*)outdata_5, _mm512_extracti64x4_epi64(d1, 1));
        
        outdata_0 += 8;
        outdata_1 += 8;
        outdata_2 += 8;
        outdata_3 += 8;
        outdata_4 += 8;
        outdata_5 += 8;

        i -= 24 * sizeof(uint64_t);
        inptr64 += 24;
    }

    while(i >= 12 * sizeof(uint64_t))
    {
        __m512i in0 = _mm512_maskz_loadu_epi64(0b00111111, inptr64 +  0);
        __m512i in1 = _mm512_maskz_loadu_epi64(0b00111111, inptr64 +  6);
        
        __m512i c0, c1;
        AVX512_INTERLEAVE6(in0, in1, c0, c1);
        
        _mm_store_si128((__m128i*)outdata_0, _mm512_castsi512_si128(c0));
        _mm_store_si128((__m128i*)outdata_1, _mm512_castsi512_si128(c1));
        _mm_store_si128((__m128i*)outdata_2, _mm512_extracti32x4_epi32(c0, 1));
        _mm_store_si128((__m128i*)outdata_3, _mm512_extracti32x4_epi32(c1, 1));
        _mm_store_si128((__m128i*)outdata_4, _mm512_extracti32x4_epi32(c0, 2));
        _mm_store_si128((__m128i*)outdata_5, _mm512_extracti32x4_epi32(c1, 2));
        
        outdata_0 += 4;
        outdata_1 += 4;
        outdata_2 += 4;
        outdata_3 += 4;
        outdata_4 += 4;
        outdata_5 += 4;

        i -= 12 * sizeof(uint64_t);
        inptr64 += 12;
    }
    
#undef AVX512_INTERLEAVE6

    //Generic block
    {
        const uint32_t* indata = (uint32_t*)inptr64;
        #include "conv_ci16_6ci16_generic.inc"
    }
}

#undef TEMPLATE_FUNC_NAME
