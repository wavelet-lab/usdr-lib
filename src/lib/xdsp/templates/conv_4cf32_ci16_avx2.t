static
void TEMPLATE_FUNC_NAME(const void *__restrict indata_0_p,
                        const void *__restrict indata_1_p,
                        const void *__restrict indata_2_p,
                        const void *__restrict indata_3_p,
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
    const float* indata_3 = (const float*)indata_3_p;
    uint64_t* outdata = (uint64_t*)outdata_p;

/*
*  |              (3)              |              (2)              |              (1)              |              (0)              |
*  +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
*  | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 |
*  +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
*  |  f25          |  f24          |  f17          |  f16          |  f9           |  f8           |  f1           |  f0           |
*  |  f27          |  f26          |  f19          |  f18          |  f11          |  f10          |  f3           |  f2           |
*  |  f29          |  f28          |  f21          |  f20          |  f13          |  f12          |  f5           |  f4           |
*  |  f31          |  f30          |  f23          |  f22          |  f15          |  f14          |  f7           |  f6           |
*
*  _mm256_packs_epi32:
*                  ++++++++++++++<--->++++++++++++++               |               ++++++++++++++<--->++++++++++++++
*  |  f27  |  f26  |  f19  |  f18  |  f25  |  f24  |  f17  |  f16  |  f11  |  f10  |  f3   |  f2   |  f9   |  f8   |  f1   |  f0   |
*  |  f31  |  f30  |  f23  |  f22  |  f29  |  f28  |  f21  |  f20  |  f15  |  f14  |  f7   |  f6   |  f13  |  f12  |  f5   |  f4   |
*                  ++++++++++++++<--->++++++++++++++               |               ++++++++++++++<--->++++++++++++++
*
*  _mm256_shuffle_epi32:
*  |  f27  |  f26  |  f25  |  f24  |  f19  |  f18  |  f17  |  f16  |  f11  |  f10  |  f9   |  f8   |  f3   |  f2   |  f1   |  f0   |
*  +-------------------------------+                               +-------------------------------+                               |
*                  \<----------------------------->\                               \<----------------------------->\
*                                  +-------------------------------+                               +-------------------------------+
*  |  f31  |  f30  |  f29  |  f28  |  f23  |  f22  |  f21  |  f20  |  f15  |  f14  |  f13  |  f12  |  f7   |  f6   |  f5   |  f4   |
*
*  _mm256_shuffle_pd:
*  |  f23  |  f22  |  f21  |  f20  |  f19  |  f18  |  f17  |  f16  |  f7   |  f6   |  f5   |  f4   |  f3   |  f2   |  f1   |  f0   |
*  |  f31  |  f30  |  f29  |  f28  |  f27  |  f26  |  f25  |  f24  |  f15  |  f14  |  f13  |  f12  |  f11  |  f10  |  f9   |  f8   |
*
*  _mm256_permute2x128_si256:
*  |  f15  |  f14  |  f13  |  f12  |  f11  |  f10  |  f9   |  f8   |  f7   |  f6   |  f5   |  f4   |  f3   |  f2   |  f1   |  f0   |
*  |  f31  |  f30  |  f29  |  f28  |  f27  |  f26  |  f25  |  f24  |  f23  |  f22  |  f21  |  f20  |  f19  |  f18  |  f17  |  f16  |
*/

    const __m256  scale = _mm256_set1_ps(1.0f / CONV_SCALE);

    while(i >= 32*4)
    {
        __m256 f0 = _mm256_loadu_ps(indata_0);
        __m256 f1 = _mm256_loadu_ps(indata_1);
        __m256 f2 = _mm256_loadu_ps(indata_2);
        __m256 f3 = _mm256_loadu_ps(indata_3);

        __m256i i0 = _mm256_cvtps_epi32(_mm256_mul_ps(f0, scale));
        __m256i i1 = _mm256_cvtps_epi32(_mm256_mul_ps(f1, scale));
        __m256i i2 = _mm256_cvtps_epi32(_mm256_mul_ps(f2, scale));
        __m256i i3 = _mm256_cvtps_epi32(_mm256_mul_ps(f3, scale));

        __m256i ii0 = _mm256_shuffle_epi32(_mm256_packs_epi32(i0, i1), _MM_SHUFFLE(3,1,2,0));
        __m256i ii1 = _mm256_shuffle_epi32(_mm256_packs_epi32(i2, i3), _MM_SHUFFLE(3,1,2,0));

        __m256i z0 = _mm256_castpd_si256(_mm256_shuffle_pd(_mm256_castsi256_pd(ii0), _mm256_castsi256_pd(ii1), 0b0000));
        __m256i z1 = _mm256_castpd_si256(_mm256_shuffle_pd(_mm256_castsi256_pd(ii0), _mm256_castsi256_pd(ii1), 0b1111));

        _mm256_storeu_si256((__m256i*)(outdata + 0), _mm256_permute2x128_si256(z0, z1, 0b00100000));
        _mm256_storeu_si256((__m256i*)(outdata + 4), _mm256_permute2x128_si256(z0, z1, 0b00110001));

        outdata += 8;
        indata_0 += 8;
        indata_1 += 8;
        indata_2 += 8;
        indata_3 += 8;
        i -= 32*4;
    }

#undef I16RND
#define I16RND(x) x > 0 ? (int16_t)(x + 0.5f) : (int16_t)(x - 0.5f)

    for (; i >= 32; i -= 32)
    {
        const float fi0 = *(indata_0++) / CONV_SCALE;
        const float fq0 = *(indata_0++) / CONV_SCALE;
        const float fi1 = *(indata_1++) / CONV_SCALE;
        const float fq1 = *(indata_1++) / CONV_SCALE;
        const float fi2 = *(indata_2++) / CONV_SCALE;
        const float fq2 = *(indata_2++) / CONV_SCALE;
        const float fi3 = *(indata_3++) / CONV_SCALE;
        const float fq3 = *(indata_3++) / CONV_SCALE;

        const int16_t i0 = I16RND(fi0);
        const int16_t q0 = I16RND(fq0);
        const int16_t i1 = I16RND(fi1);
        const int16_t q1 = I16RND(fq1);
        const int16_t i2 = I16RND(fi2);
        const int16_t q2 = I16RND(fq2);
        const int16_t i3 = I16RND(fi3);
        const int16_t q3 = I16RND(fq3);

        *outdata++ = (uint64_t)(uint16_t)i0 | ((uint64_t)(uint16_t)q0 << 16) | ((uint64_t)(uint16_t)i1 << 32) | ((uint64_t)(uint16_t)q1 << 48);
        *outdata++ = (uint64_t)(uint16_t)i2 | ((uint64_t)(uint16_t)q2 << 16) | ((uint64_t)(uint16_t)i3 << 32) | ((uint64_t)(uint16_t)q3 << 48);
    }

    // do nothing with leftover
}

#undef TEMPLATE_FUNC_NAME
