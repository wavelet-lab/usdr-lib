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

    const long long int* inptr64 = (long long int*)indata_p;
    const __m512 scale = _mm512_set1_ps(CONV_SCALE);
    const __m256 scale_avx2 = _mm256_set1_ps(CONV_SCALE);

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
        
        __m512 r0 = _mm512_cvtepi32_ps(_mm512_cvtepi16_epi32(_mm512_castsi512_si256(d0)));
        __m512 r1 = _mm512_cvtepi32_ps(_mm512_cvtepi16_epi32(_mm512_castsi512_si256(d1)));
        __m512 r2 = _mm512_cvtepi32_ps(_mm512_cvtepi16_epi32(_mm512_castsi512_si256(d2)));
        __m512 r3 = _mm512_cvtepi32_ps(_mm512_cvtepi16_epi32(_mm512_castsi512_si256(d3)));
        __m512 r4 = _mm512_cvtepi32_ps(_mm512_cvtepi16_epi32(_mm512_extracti64x4_epi64(d0, 1)));
        __m512 r5 = _mm512_cvtepi32_ps(_mm512_cvtepi16_epi32(_mm512_extracti64x4_epi64(d1, 1)));

        _mm512_store_ps(outdata_0, _mm512_mul_ps(r0, scale));
        _mm512_store_ps(outdata_1, _mm512_mul_ps(r1, scale));
        _mm512_store_ps(outdata_2, _mm512_mul_ps(r2, scale));
        _mm512_store_ps(outdata_3, _mm512_mul_ps(r3, scale));
        _mm512_store_ps(outdata_4, _mm512_mul_ps(r4, scale));
        _mm512_store_ps(outdata_5, _mm512_mul_ps(r5, scale));
        
        outdata_0 += 16;
        outdata_1 += 16;
        outdata_2 += 16;
        outdata_3 += 16;
        outdata_4 += 16;
        outdata_5 += 16;

        i -= 24 * sizeof(uint64_t);
        inptr64 += 24;
    }

    while(i >= 12 * sizeof(uint64_t))
    {
        __m512i in0 = _mm512_maskz_loadu_epi64(0b00111111, inptr64 +  0);
        __m512i in1 = _mm512_maskz_loadu_epi64(0b00111111, inptr64 +  6);
        
        __m512i c0, c1;
        AVX512_INTERLEAVE6(in0, in1, c0, c1);
        
        __m256 r0 = _mm256_cvtepi32_ps(_mm256_cvtepi16_epi32(_mm512_castsi512_si128(c0)));
        __m256 r1 = _mm256_cvtepi32_ps(_mm256_cvtepi16_epi32(_mm512_castsi512_si128(c1)));
        __m256 r2 = _mm256_cvtepi32_ps(_mm256_cvtepi16_epi32(_mm512_extracti32x4_epi32(c0, 1)));
        __m256 r3 = _mm256_cvtepi32_ps(_mm256_cvtepi16_epi32(_mm512_extracti32x4_epi32(c1, 1)));
        __m256 r4 = _mm256_cvtepi32_ps(_mm256_cvtepi16_epi32(_mm512_extracti32x4_epi32(c0, 2)));
        __m256 r5 = _mm256_cvtepi32_ps(_mm256_cvtepi16_epi32(_mm512_extracti32x4_epi32(c1, 2)));

        _mm256_store_ps(outdata_0, _mm256_mul_ps(r0, scale_avx2));
        _mm256_store_ps(outdata_1, _mm256_mul_ps(r1, scale_avx2));
        _mm256_store_ps(outdata_2, _mm256_mul_ps(r2, scale_avx2));
        _mm256_store_ps(outdata_3, _mm256_mul_ps(r3, scale_avx2));
        _mm256_store_ps(outdata_4, _mm256_mul_ps(r4, scale_avx2));
        _mm256_store_ps(outdata_5, _mm256_mul_ps(r5, scale_avx2));
        
        outdata_0 += 8;
        outdata_1 += 8;
        outdata_2 += 8;
        outdata_3 += 8;
        outdata_4 += 8;
        outdata_5 += 8;

        i -= 12 * sizeof(uint64_t);
        inptr64 += 12;
    }
    
#undef AVX512_INTERLEAVE6

    //Generic block
    {
        const int16_t *in = (const int16_t *)inptr64;
        #include "conv_ci16_6cf32_generic.inc"
    }
}

#undef TEMPLATE_FUNC_NAME
