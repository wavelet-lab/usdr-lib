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
    if ((outdatabsz) < i)
        i = (outdatabsz);

    uint64_t* outdata = (uint64_t*)outdata_p;

    const __m256i* vp0 = (const __m256i* )indata_0_p;
    const __m256i* vp1 = (const __m256i* )indata_1_p;
    const __m256i* vp2 = (const __m256i* )indata_2_p;
    const __m256i* vp3 = (const __m256i* )indata_3_p;

/*
*  |              (3)              |              (2)              |              (1)              |              (0)              |
*  +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
*  | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 |
*  +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
* ALGO1
*  r0..3:
*  |  a57  |  a56  |  a49  |  a48  |  a41  |  a40  |  a33  |  a32  |  a25  |  a24  |  a17  |  a16  |  a9   |  a8   |  a1   |  a0   |
*  |  a59  |  a58  |  a51  |  a50  |  a43  |  a42  |  a35  |  a34  |  a27  |  a26  |  a19  |  a18  |  a11  |  a10  |  a3   |  a2   |
*  |  a61  |  a60  |  a53  |  a52  |  a45  |  a44  |  a37  |  a36  |  a29  |  a28  |  a21  |  a20  |  a13  |  a12  |  a5   |  a4   |
*  |  a63  |  a62  |  a55  |  a54  |  a47  |  a46  |  a39  |  a38  |  a31  |  a30  |  a23  |  a22  |  a15  |  a14  |  a7   |  a6   |
*
*  _mm256_shuffle_pd:
*  |  a43  |  a42  |  a35  |  a34  |  a41  |  a40  |  a33  |  a32  |  a11  |  a10  |  a3   |  a2   |  a9   |  a8   |  a1   |  a0   |
*  |  a59  |  a58  |  a51  |  a50  |  a57  |  a56  |  a49  |  a48  |  a27  |  a26  |  a19  |  a18  |  a25  |  a24  |  a17  |  a16  |
*  |  a47  |  a46  |  a39  |  a38  |  a45  |  a44  |  a37  |  a36  |  a15  |  a14  |  a7   |  a6   |  a13  |  a12  |  a5   |  a4   |
*  |  a63  |  a62  |  a55  |  a54  |  a61  |  a60  |  a53  |  a52  |  a31  |  a30  |  a23  |  a22  |  a29  |  a28  |  a21  |  a20  |
*  |       7       |       6       |       5       |       4       |       3       |       2       |       1       |       0       |
*
* _mm256_permute2x128_si256:
*  |  a15  |  a14  |  a7   |  a6   |  a13  |  a12  |  a5   |  a4   |  a11  |  a10  |  a3   |  a2   |  a9   |  a8   |  a1   |  a0   |
*  |  a31  |  a30  |  a23  |  a22  |  a29  |  a28  |  a21  |  a20  |  a27  |  a26  |  a19  |  a18  |  a25  |  a24  |  a17  |  a16  |
*  |  a47  |  a46  |  a39  |  a38  |  a45  |  a44  |  a37  |  a36  |  a43  |  a42  |  a35  |  a34  |  a41  |  a40  |  a33  |  a32  |
*  |  a63  |  a62  |  a55  |  a54  |  a61  |  a60  |  a53  |  a52  |  a59  |  a58  |  a51  |  a50  |  a57  |  a56  |  a49  |  a48  |
*
* _mm256_permutevar8x32_epi32:
*  |  a15  |  a14  |  a13  |  a12  |  a11  |  a10  |  a9   |  a8   |  a7   |  a6   |  a5   |  a4   |  a3   |  a2   |  a1   |  a0   |
*  |  a31  |  a30  |  a29  |  a28  |  a27  |  a26  |  a25  |  a24  |  a23  |  a22  |  a21  |  a20  |  a19  |  a18  |  a17  |  a16  |
*  |  a47  |  a46  |  a45  |  a44  |  a43  |  a42  |  a41  |  a40  |  a39  |  a38  |  a37  |  a36  |  a35  |  a34  |  a33  |  a32  |
*  |  a63  |  a62  |  a61  |  a60  |  a59  |  a58  |  a57  |  a56  |  a55  |  a54  |  a53  |  a52  |  a51  |  a50  |  a49  |  a48  |
*
* ALGO2
* load
*  |       c28     |       c24     |       c20     |       c16     |       c12     |       c8      |       c4      |       c0      |
*  |       c29     |       c25     |       c21     |       c17     |       c13     |       c9      |       c5      |       c1      |
*  |       c30     |       c26     |       c22     |       c18     |       c14     |       c10     |       c6      |       c2      |
*  |       c31     |       c27     |       c23     |       c19     |       c15     |       c11     |       c7      |       c3      |

* _mm256_shuffle_pd
*  |      >c21     |      >c17     |       c20     |       c16     |      >c5      |      >c1      |       c4      |       c0      |
*  |       c29     |       c25     |      <c28     |      <c24     |       c13     |       c9      |      <c12     |      <c8      |
*  |      >c23     |      >c19     |       c22     |       c18     |      >c7      |      >c3      |       c6      |       c2      |
*  |       c31     |       c27     |      <c30     |      <c26     |       c15     |       c11     |      <c14     |      <c10     |

* shuffle interlain epi32    <-------------->                                              <---------------->
*  |       c21     |       c20     |       c17     |       c16     |       c5      |       c4      |       c1      |       c0      |
*  |       c29     |       c28     |       c25     |       c24     |       c13     |       c12     |       c9      |       c8      |
*  |       c23     |       c22     |       c19     |       c18     |       c7      |       c6      |       c3      |       c2      |
*  |       c31     |       c30     |       c27     |       c26     |       c15     |       c14     |       c11     |       c10     |

* _mm256_shuffle_pd
*  |      >c19     |      >c18     |       c17     |       c16     |      >c3      |      >c2      |       c1      |       c0      |
*  |       c23     |       c22     |      <c21     |      <c20     |       c7      |       c6      |      <c5      |      <c4      |
*  |      >c27     |      >c26     |       c25     |       c24     |      >c11     |      >c10     |       c9      |       c8      |
*  |       c31     |       c30     |      <c29     |      <c28     |       c15     |       c14     |      <c13     |      <c12     |
*/

    __m256i r0, r1, r2, r3;
    __m256i a0, a1, a2, a3;
    __m256i b0, b1, b2, b3;
    __m256i c0, c1, c2, c3;

#undef ALGO1

#ifdef ALGO1
const __m256i permmask = _mm256_set_epi32(7, 5, 3, 1, 6, 4, 2, 0);
#endif

    for(; i >= 128; i -= 128)
    {
        r0 = _mm256_loadu_si256(vp0++);
        r1 = _mm256_loadu_si256(vp1++);
        r2 = _mm256_loadu_si256(vp2++);
        r3 = _mm256_loadu_si256(vp3++);

#ifdef ALGO1
        a0 = _mm256_castpd_si256(_mm256_shuffle_pd(r0, r1, 0b0000));
        a1 = _mm256_castpd_si256(_mm256_shuffle_pd(r0, r1, 0b1111));
        a2 = _mm256_castpd_si256(_mm256_shuffle_pd(r2, r3, 0b0000));
        a3 = _mm256_castpd_si256(_mm256_shuffle_pd(r2, r3, 0b1111));

        b0 = _mm256_permute2x128_si256(a0, a2, 0b00100000);
        b1 = _mm256_permute2x128_si256(a1, a3, 0b00100000);
        b2 = _mm256_permute2x128_si256(a0, a2, 0b00110001);
        b3 = _mm256_permute2x128_si256(a1, a3, 0b00110001);

        c0 = _mm256_permutevar8x32_epi32(b0, permmask);
        c1 = _mm256_permutevar8x32_epi32(b1, permmask);
        c2 = _mm256_permutevar8x32_epi32(b2, permmask);
        c3 = _mm256_permutevar8x32_epi32(b3, permmask);
#else
        a0 = _mm256_castpd_si256(_mm256_shuffle_pd(r0, r1, 0b0000));
        a1 = _mm256_castpd_si256(_mm256_shuffle_pd(r0, r1, 0b1111));
        a2 = _mm256_castpd_si256(_mm256_shuffle_pd(r2, r3, 0b0000));
        a3 = _mm256_castpd_si256(_mm256_shuffle_pd(r2, r3, 0b1111));

        a0 = _mm256_shuffle_epi32(a0, _MM_SHUFFLE(3,1,2,0));
        a1 = _mm256_shuffle_epi32(a1, _MM_SHUFFLE(3,1,2,0));
        a2 = _mm256_shuffle_epi32(a2, _MM_SHUFFLE(3,1,2,0));
        a3 = _mm256_shuffle_epi32(a3, _MM_SHUFFLE(3,1,2,0));

        b0 = _mm256_castpd_si256(_mm256_shuffle_pd(a0, a2, 0b0000));
        b1 = _mm256_castpd_si256(_mm256_shuffle_pd(a0, a2, 0b1111));
        b2 = _mm256_castpd_si256(_mm256_shuffle_pd(a1, a3, 0b0000));
        b3 = _mm256_castpd_si256(_mm256_shuffle_pd(a1, a3, 0b1111));

        c0 = _mm256_permute2x128_si256(b0, b1, 0b00100000);
        c1 = _mm256_permute2x128_si256(b2, b3, 0b00100000);
        c2 = _mm256_permute2x128_si256(b0, b1, 0b00110001);
        c3 = _mm256_permute2x128_si256(b2, b3, 0b00110001);
#endif
        _mm256_storeu_si256((__m256i*)(outdata +  0), c0);
        _mm256_storeu_si256((__m256i*)(outdata +  4), c1);
        _mm256_storeu_si256((__m256i*)(outdata +  8), c2);
        _mm256_storeu_si256((__m256i*)(outdata + 12), c3);

        outdata += 16;
    }

    const uint32_t* indata_0 = (uint32_t*)vp0;
    const uint32_t* indata_1 = (uint32_t*)vp1;
    const uint32_t* indata_2 = (uint32_t*)vp2;
    const uint32_t* indata_3 = (uint32_t*)vp3;

    for (; i >= 16; i -= 16)
    {
        const uint32_t iq0 = *indata_0++;
        const uint32_t iq1 = *indata_1++;
        const uint32_t iq2 = *indata_2++;
        const uint32_t iq3 = *indata_3++;

        *(uint64_t*)outdata++ = (uint64_t)iq0 | ((uint64_t)iq1 << 32);
        *(uint64_t*)outdata++ = (uint64_t)iq2 | ((uint64_t)iq3 << 32);
    }

    // do nothing with leftover
}

#undef TEMPLATE_FUNC_NAME
