static
void TEMPLATE_FUNC_NAME(const void *__restrict indata_p,
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
    if ((outdatabsz) < i)
        i = (outdatabsz);

    const __m256i * indata = (__m256i*)indata_p;

    uint32_t* outdata_0 = (uint32_t*)outdata_0_p;
    uint32_t* outdata_1 = (uint32_t*)outdata_1_p;
    uint32_t* outdata_2 = (uint32_t*)outdata_2_p;
    uint32_t* outdata_3 = (uint32_t*)outdata_3_p;
    uint32_t* outdata_4 = (uint32_t*)outdata_4_p;
    uint32_t* outdata_5 = (uint32_t*)outdata_5_p;
    uint32_t* outdata_6 = (uint32_t*)outdata_6_p;
    uint32_t* outdata_7 = (uint32_t*)outdata_7_p;

#undef USE_SSE_STORES

    //AVX2
    {
        __m256i in0, in1, in2, in3, in4, in5, in6, in7;
        __m256i a0, a1, a2, a3, a4, a5, a6, a7;
        __m256i b0, b1, b2, b3, b4, b5, b6, b7;
#ifndef USE_SSE_STORES
        __m256i out0, out1, out2, out3, out4, out5, out6, out7;
#endif

        for(; i >= 32 * 8; i -= 32 * 8)
        {
            in0 = _mm256_load_si256(indata++);
            in1 = _mm256_load_si256(indata++);
            in2 = _mm256_load_si256(indata++);
            in3 = _mm256_load_si256(indata++);

            in0 = _mm256_shuffle_epi32(in0, _MM_SHUFFLE(3, 1, 2, 0));
            in1 = _mm256_shuffle_epi32(in1, _MM_SHUFFLE(3, 1, 2, 0));
            in2 = _mm256_shuffle_epi32(in2, _MM_SHUFFLE(3, 1, 2, 0));
            in3 = _mm256_shuffle_epi32(in3, _MM_SHUFFLE(3, 1, 2, 0));

            a0 = _mm256_castpd_si256(_mm256_shuffle_pd(_mm256_castsi256_pd(in0), _mm256_castsi256_pd(in1), 0b0000));
            a1 = _mm256_castpd_si256(_mm256_shuffle_pd(_mm256_castsi256_pd(in0), _mm256_castsi256_pd(in1), 0b1111));
            a2 = _mm256_castpd_si256(_mm256_shuffle_pd(_mm256_castsi256_pd(in2), _mm256_castsi256_pd(in3), 0b0000));
            a3 = _mm256_castpd_si256(_mm256_shuffle_pd(_mm256_castsi256_pd(in2), _mm256_castsi256_pd(in3), 0b1111));

            a0 = _mm256_shuffle_epi32(a0, _MM_SHUFFLE(3, 1, 2, 0));
            a1 = _mm256_shuffle_epi32(a1, _MM_SHUFFLE(3, 1, 2, 0));
            a2 = _mm256_shuffle_epi32(a2, _MM_SHUFFLE(3, 1, 2, 0));
            a3 = _mm256_shuffle_epi32(a3, _MM_SHUFFLE(3, 1, 2, 0));

            b0 = _mm256_castpd_si256(_mm256_shuffle_pd(_mm256_castsi256_pd(a0), _mm256_castsi256_pd(a2), 0b0000));
            b1 = _mm256_castpd_si256(_mm256_shuffle_pd(_mm256_castsi256_pd(a1), _mm256_castsi256_pd(a3), 0b0000));
            b2 = _mm256_castpd_si256(_mm256_shuffle_pd(_mm256_castsi256_pd(a0), _mm256_castsi256_pd(a2), 0b1111));
            b3 = _mm256_castpd_si256(_mm256_shuffle_pd(_mm256_castsi256_pd(a1), _mm256_castsi256_pd(a3), 0b1111));

            //

            in4 = _mm256_load_si256(indata++);
            in5 = _mm256_load_si256(indata++);
            in6 = _mm256_load_si256(indata++);
            in7 = _mm256_load_si256(indata++);

            in4 = _mm256_shuffle_epi32(in4, _MM_SHUFFLE(3, 1, 2, 0));
            in5 = _mm256_shuffle_epi32(in5, _MM_SHUFFLE(3, 1, 2, 0));
            in6 = _mm256_shuffle_epi32(in6, _MM_SHUFFLE(3, 1, 2, 0));
            in7 = _mm256_shuffle_epi32(in7, _MM_SHUFFLE(3, 1, 2, 0));

            a4 = _mm256_castpd_si256(_mm256_shuffle_pd(_mm256_castsi256_pd(in4), _mm256_castsi256_pd(in5), 0b0000));
            a5 = _mm256_castpd_si256(_mm256_shuffle_pd(_mm256_castsi256_pd(in4), _mm256_castsi256_pd(in5), 0b1111));
            a6 = _mm256_castpd_si256(_mm256_shuffle_pd(_mm256_castsi256_pd(in6), _mm256_castsi256_pd(in7), 0b0000));
            a7 = _mm256_castpd_si256(_mm256_shuffle_pd(_mm256_castsi256_pd(in6), _mm256_castsi256_pd(in7), 0b1111));

            a4 = _mm256_shuffle_epi32(a4, _MM_SHUFFLE(3, 1, 2, 0));
            a5 = _mm256_shuffle_epi32(a5, _MM_SHUFFLE(3, 1, 2, 0));
            a6 = _mm256_shuffle_epi32(a6, _MM_SHUFFLE(3, 1, 2, 0));
            a7 = _mm256_shuffle_epi32(a7, _MM_SHUFFLE(3, 1, 2, 0));

            b4 = _mm256_castpd_si256(_mm256_shuffle_pd(_mm256_castsi256_pd(a4), _mm256_castsi256_pd(a6), 0b0000));
            b5 = _mm256_castpd_si256(_mm256_shuffle_pd(_mm256_castsi256_pd(a5), _mm256_castsi256_pd(a7), 0b0000));
            b6 = _mm256_castpd_si256(_mm256_shuffle_pd(_mm256_castsi256_pd(a4), _mm256_castsi256_pd(a6), 0b1111));
            b7 = _mm256_castpd_si256(_mm256_shuffle_pd(_mm256_castsi256_pd(a5), _mm256_castsi256_pd(a7), 0b1111));

            //

#ifdef USE_SSE_STORES

//#define _MM_STORE_FN _mm_store_si128
#define _MM_STORE_FN _mm_storeu_si128

            _MM_STORE_FN((__m128i*)outdata_0, _mm256_castsi256_si128(b0));
            _MM_STORE_FN((__m128i*)outdata_1, _mm256_castsi256_si128(b1));
            _MM_STORE_FN((__m128i*)outdata_2, _mm256_castsi256_si128(b2));
            _MM_STORE_FN((__m128i*)outdata_3, _mm256_castsi256_si128(b3));
            _MM_STORE_FN((__m128i*)outdata_4, _mm256_extractf128_si256(b0, 1));
            _MM_STORE_FN((__m128i*)outdata_5, _mm256_extractf128_si256(b1, 1));
            _MM_STORE_FN((__m128i*)outdata_6, _mm256_extractf128_si256(b2, 1));
            _MM_STORE_FN((__m128i*)outdata_7, _mm256_extractf128_si256(b3, 1));

            _MM_STORE_FN((__m128i*)(outdata_0 + 4), _mm256_castsi256_si128(b4));
            _MM_STORE_FN((__m128i*)(outdata_1 + 4), _mm256_castsi256_si128(b5));
            _MM_STORE_FN((__m128i*)(outdata_2 + 4), _mm256_castsi256_si128(b6));
            _MM_STORE_FN((__m128i*)(outdata_3 + 4), _mm256_castsi256_si128(b7));
            _MM_STORE_FN((__m128i*)(outdata_4 + 4), _mm256_extractf128_si256(b4, 1));
            _MM_STORE_FN((__m128i*)(outdata_5 + 4), _mm256_extractf128_si256(b5, 1));
            _MM_STORE_FN((__m128i*)(outdata_6 + 4), _mm256_extractf128_si256(b6, 1));
            _MM_STORE_FN((__m128i*)(outdata_7 + 4), _mm256_extractf128_si256(b7, 1));
#else

//#define _MM256_STORE_FN _mm256_store_si256
#define _MM256_STORE_FN _mm256_storeu_si256

            out0 = _mm256_permute2x128_si256(b0, b4, 0b00100000);
            out1 = _mm256_permute2x128_si256(b1, b5, 0b00100000);
            out2 = _mm256_permute2x128_si256(b2, b6, 0b00100000);
            out3 = _mm256_permute2x128_si256(b3, b7, 0b00100000);

            _MM256_STORE_FN((__m256i*)outdata_0, out0);
            _MM256_STORE_FN((__m256i*)outdata_1, out1);
            _MM256_STORE_FN((__m256i*)outdata_2, out2);
            _MM256_STORE_FN((__m256i*)outdata_3, out3);

            out4 = _mm256_permute2x128_si256(b0, b4, 0b00110001);
            out5 = _mm256_permute2x128_si256(b1, b5, 0b00110001);
            out6 = _mm256_permute2x128_si256(b2, b6, 0b00110001);
            out7 = _mm256_permute2x128_si256(b3, b7, 0b00110001);

            _MM256_STORE_FN((__m256i*)outdata_4, out4);
            _MM256_STORE_FN((__m256i*)outdata_5, out5);
            _MM256_STORE_FN((__m256i*)outdata_6, out6);
            _MM256_STORE_FN((__m256i*)outdata_7, out7);
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
        const uint32_t* in = (uint32_t*)indata;
        #include "conv_ci16_8ci16_generic.inc"
    }
}

#undef TEMPLATE_FUNC_NAME
