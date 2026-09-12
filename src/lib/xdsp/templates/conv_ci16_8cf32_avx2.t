static
void TEMPLATE_FUNC_NAME(const void *__restrict indata,
                        unsigned indatabsz,
                        void *__restrict outdata_0_p,
                        void *__restrict outdata_1_p,
                        void *__restrict outdata_2_p,
                        void *__restrict outdata_3_p,
                        void *__restrict outdata_4_p,
                        void *__restrict outdata_5_p,
                        void *__restrict outdata_6_p,
                        void *__restrict outdata_7_p,
                        unsigned outdatabsz)
{
    unsigned i = indatabsz;
    if ((outdatabsz / 2) < i)
        i = (outdatabsz / 2);

    const __m256i * indata_p = (__m256i*)indata;

    float* outdata_0 = (float*)outdata_0_p;
    float* outdata_1 = (float*)outdata_1_p;
    float* outdata_2 = (float*)outdata_2_p;
    float* outdata_3 = (float*)outdata_3_p;
    float* outdata_4 = (float*)outdata_4_p;
    float* outdata_5 = (float*)outdata_5_p;
    float* outdata_6 = (float*)outdata_6_p;
    float* outdata_7 = (float*)outdata_7_p;

#define USE_SSE_STORES

    //AVX2
    {
        const __m256  scale = _mm256_set1_ps(CONV_SCALE);

        __m256i in0, in1, in2, in3;
        __m256d f0a, f0b, f1a, f1b, f2a, f2b, f3a, f3b;
        __m256d x0a, x0b, x1a, x1b, x2a, x2b, x3a, x3b;
#ifndef USE_SSE_STORES
        __m256d out0, out1, out2, out3, out4, out5, out6, out7;
#endif

        for (; i >= 32 * 4; i -= 32 * 4)
        {
            in0 = _mm256_load_si256(indata_p++);
            in1 = _mm256_load_si256(indata_p++);
            in2 = _mm256_load_si256(indata_p++);
            in3 = _mm256_load_si256(indata_p++);

            f0a = _mm256_castps_pd(_mm256_mul_ps(_mm256_cvtepi32_ps(_mm256_cvtepi16_epi32(_mm256_castsi256_si128(in0))), scale));
            f0b = _mm256_castps_pd(_mm256_mul_ps(_mm256_cvtepi32_ps(_mm256_cvtepi16_epi32(_mm256_extracti128_si256(in0, 1))), scale));
            f1a = _mm256_castps_pd(_mm256_mul_ps(_mm256_cvtepi32_ps(_mm256_cvtepi16_epi32(_mm256_castsi256_si128(in1))), scale));
            f1b = _mm256_castps_pd(_mm256_mul_ps(_mm256_cvtepi32_ps(_mm256_cvtepi16_epi32(_mm256_extracti128_si256(in1, 1))), scale));
            f2a = _mm256_castps_pd(_mm256_mul_ps(_mm256_cvtepi32_ps(_mm256_cvtepi16_epi32(_mm256_castsi256_si128(in2))), scale));
            f2b = _mm256_castps_pd(_mm256_mul_ps(_mm256_cvtepi32_ps(_mm256_cvtepi16_epi32(_mm256_extracti128_si256(in2, 1))), scale));
            f3a = _mm256_castps_pd(_mm256_mul_ps(_mm256_cvtepi32_ps(_mm256_cvtepi16_epi32(_mm256_castsi256_si128(in3))), scale));
            f3b = _mm256_castps_pd(_mm256_mul_ps(_mm256_cvtepi32_ps(_mm256_cvtepi16_epi32(_mm256_extracti128_si256(in3, 1))), scale));

            x0a = _mm256_shuffle_pd(f0a, f1a, 0b0000);
            x1a = _mm256_shuffle_pd(f0a, f1a, 0b1111);
            x0b = _mm256_shuffle_pd(f0b, f1b, 0b0000);
            x1b = _mm256_shuffle_pd(f0b, f1b, 0b1111);
            x2a = _mm256_shuffle_pd(f2a, f3a, 0b0000);
            x3a = _mm256_shuffle_pd(f2a, f3a, 0b1111);
            x2b = _mm256_shuffle_pd(f2b, f3b, 0b0000);
            x3b = _mm256_shuffle_pd(f2b, f3b, 0b1111);

#ifdef USE_SSE_STORES

//#define _MM_STORE_FN _mm_store_pd
#define _MM_STORE_FN _mm_storeu_pd

            _MM_STORE_FN((double*)outdata_0, _mm256_castpd256_pd128(x0a));
            _MM_STORE_FN((double*)outdata_1, _mm256_castpd256_pd128(x1a));
            _MM_STORE_FN((double*)outdata_2, _mm256_extractf128_pd(x0a, 1));
            _MM_STORE_FN((double*)outdata_3, _mm256_extractf128_pd(x1a, 1));
            _MM_STORE_FN((double*)outdata_4, _mm256_castpd256_pd128(x0b));
            _MM_STORE_FN((double*)outdata_5, _mm256_castpd256_pd128(x1b));
            _MM_STORE_FN((double*)outdata_6, _mm256_extractf128_pd(x0b, 1));
            _MM_STORE_FN((double*)outdata_7, _mm256_extractf128_pd(x1b, 1));

            _MM_STORE_FN((double*)(outdata_0 + 4), _mm256_castpd256_pd128(x2a));
            _MM_STORE_FN((double*)(outdata_1 + 4), _mm256_castpd256_pd128(x3a));
            _MM_STORE_FN((double*)(outdata_2 + 4), _mm256_extractf128_pd(x2a, 1));
            _MM_STORE_FN((double*)(outdata_3 + 4), _mm256_extractf128_pd(x3a, 1));
            _MM_STORE_FN((double*)(outdata_4 + 4), _mm256_castpd256_pd128(x2b));
            _MM_STORE_FN((double*)(outdata_5 + 4), _mm256_castpd256_pd128(x3b));
            _MM_STORE_FN((double*)(outdata_6 + 4), _mm256_extractf128_pd(x2b, 1));
            _MM_STORE_FN((double*)(outdata_7 + 4), _mm256_extractf128_pd(x3b, 1));
#else

//#define _MM256_STORE_FN _mm256_store_pd
#define _MM256_STORE_FN _mm256_storeu_pd

            out0 = _mm256_permute2f128_pd(x0a, x2a, 0b00100000);
            out2 = _mm256_permute2f128_pd(x0a, x2a, 0b00110001);
            out1 = _mm256_permute2f128_pd(x1a, x3a, 0b00100000);
            out3 = _mm256_permute2f128_pd(x1a, x3a, 0b00110001);

            _MM256_STORE_FN((double*)outdata_0, out0);
            _MM256_STORE_FN((double*)outdata_1, out1);
            _MM256_STORE_FN((double*)outdata_2, out2);
            _MM256_STORE_FN((double*)outdata_3, out3);

            out4 = _mm256_permute2f128_pd(x0b, x2b, 0b00100000);
            out6 = _mm256_permute2f128_pd(x0b, x2b, 0b00110001);
            out5 = _mm256_permute2f128_pd(x1b, x3b, 0b00100000);
            out7 = _mm256_permute2f128_pd(x1b, x3b, 0b00110001);

            _MM256_STORE_FN((double*)outdata_4, out4);
            _MM256_STORE_FN((double*)outdata_5, out5);
            _MM256_STORE_FN((double*)outdata_6, out6);
            _MM256_STORE_FN((double*)outdata_7, out7);
#endif
            outdata_0 += 8;
            outdata_1 += 8;
            outdata_2 += 8;
            outdata_3 += 8;
            outdata_4 += 8;
            outdata_5 += 8;
            outdata_6 += 8;
            outdata_7 += 8;
        }
    }

    //Generic
    {
        const uint64_t *ld = (const uint64_t *)indata_p;
        #include "conv_ci16_8cf32_generic.inc"
    }
}

#undef TEMPLATE_FUNC_NAME
