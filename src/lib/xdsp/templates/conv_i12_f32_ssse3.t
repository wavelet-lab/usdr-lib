#define IQ12_SC32_SSSE3_EX_LOGIC
#define UNALIGN_STORE

#ifdef UNALIGN_STORE
#define _MM_STOREX_PS    _mm_storeu_ps
#define _MM256_STOREX_PS _mm256_storeu_ps
#else
#define _MM_STOREX_PS    _mm_store_ps
#define _MM256_STOREX_PS _mm256_store_ps
#endif

#define SCALE2    (CONV_SCALE / 65536)

static inline
void TEMPLATE_FUNC_NAME(const void *__restrict indata_p,
                        unsigned indatabsz,
                        void *__restrict outdata_p,
                        unsigned outdatabsz)
{
    unsigned i = indatabsz;

    /* 12 bits -> 32 bits  =>  3 -> 8   */
    if ((outdatabsz * 3 / 8) < i)
        i = (outdatabsz * 3 / 8);

    const uint8_t* indata = (const uint8_t*)indata_p;

    float* outdata = (float*)outdata_p;

    const __m128i* ld = (const __m128i*)indata_p;

    __m128  scale = _mm_set_ps(SCALE2, SCALE2, SCALE2, SCALE2);
    __m128i ands  = _mm_set_epi32(0xfff00000, 0xfff00000, 0xfff00000, 0xfff00000);

#ifdef IQ12_SC32_SSSE3_EX_LOGIC
    __m128i and_h = _mm_set_epi32(0xffffffff, 0x00000000, 0x00000000, 0x00000000);
    __m128i mx1 = _mm_set_epi8(0x7, 0x6, 0x5, 0x80,
                               0x4, 0x3, 0x2, 0x80,
                               0x1, 0x0, 0xF, 0x80,
                               0xE, 0xD, 0xC, 0x80);
    __m128i p1;
#endif

    __m128i mx0 = _mm_set_epi8(0xB, 0xA, 0x9, 0x80,
                               0x8, 0x7, 0x6, 0x80,
                               0x5, 0x4, 0x3, 0x80,
                               0x2, 0x1, 0x0, 0x80);
    __m128i mx3 = _mm_set_epi8(0xF, 0xE, 0xD, 0x80,
                               0xC, 0xB, 0xA, 0x80,
                               0x9, 0x8, 0x7, 0x80,
                               0x6, 0x5, 0x4, 0x80);
    __m128i q0, q1, q2;
    __m128i b0, b1, b2, b3, s0, s1, s2, s3, z0, z1, z2, z3;
    __m128i p0, p2, p4, p5;
    __m128 t0, t1, t2, t3;

    q0 = _mm_load_si128(ld); ld++;
    q1 = _mm_load_si128(ld); ld++;
    q2 = _mm_load_si128(ld); ld++;

    indata += 48;

    /*
     *  |       v3      |       v2      |       v1      |       v0      |
     *  +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     *  | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 |
     *  +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     *  |...| 12  |  12 | 12  |  12 | 12  |  12 | 12  |  12 | 12  |  12 |
     *  +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     *  f10L  f9     f8   f7     f6   f5     f4   f3     f2   f1     f0
     *
     *  |       v7      |       v6      |       v5      |       v4      |
     *  +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     *  | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 |
     *  +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     *  |.| 12  |  12 | 12  |  12 | 12  |  12 | 12  |  12 | 12  |  12 |.|
     *  +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     * f21L f20   f19   f18   f17   f16   f15   f14   f13   f12   f11 f10H
     *
     *  |       v11     |       v10     |       v9      |       v8      |
     *  +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     *  | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 |
     *  +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     *  | 12  |  12 | 12  |  12 | 12  |  12 | 12  |  12 | 12  |  12 |...|
     *  +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
     *    f31   f30   f29   f28   f27   f26   f25   f24   f23   f22  f21H
     */

    for (; i >= 48; i -= 48) {
        z0 = _mm_shuffle_epi8(q0, mx0); // f7f6_f5f4_f3f2_f1f0
        z3 = _mm_shuffle_epi8(q2, mx3); // f31f30_f29f28_f27f26_f25f24

#ifdef IQ12_SC32_SSSE3_EX_LOGIC
        p0 = _mm_and_si128(and_h, q0);
        p1 = _mm_andnot_si128(and_h, q1);
        p4 = _mm_or_si128(p0, p1);
#else
        p0 = _mm_shuffle_epi32(q0, _MM_SHUFFLE(1, 0, 3, 2));
        p4 = _mm_unpacklo_epi64(p0, q1); // f16.....f5H
#endif

        p2 = _mm_shuffle_epi32(q1, _MM_SHUFFLE(1, 0, 3, 2));
        p5 = _mm_unpacklo_epi64(p2, q2); // f26L.....f16

#ifdef IQ12_SC32_SSSE3_EX_LOGIC
        z1 = _mm_shuffle_epi8(p4, mx1); // f15.._..f8
#else
        z1 = _mm_shuffle_epi8(p4, mx3); // f15.._..f8
#endif
        z2 = _mm_shuffle_epi8(p5, mx0); // f23.._..f16


        q0 = _mm_load_si128(ld); ld++;
        q1 = _mm_load_si128(ld); ld++;
        q2 = _mm_load_si128(ld); ld++;

        indata += 48;

        b0 = _mm_slli_epi32(z0, 12);     // [f6;  f4;  f2;  f0]
        b1 = _mm_and_si128(z0, ands);    // [f7;  f5;  f3;  f1]
        b2 = _mm_slli_epi32(z1, 12);     // [f14; f12; f10; f8]
        b3 = _mm_and_si128(z1, ands);    // [f15; f13; f11; f9]

        s0 = _mm_unpacklo_epi32(b0, b1); // [f3;  f2;  f2;  f0]
        s1 = _mm_unpackhi_epi32(b0, b1); // [f7;  f6;  f5;  f4]
        s2 = _mm_unpacklo_epi32(b2, b3); // [f11; f10; f9;  f8]
        s3 = _mm_unpackhi_epi32(b2, b3); // [f15; f14; f13; f12]

        t0 = _mm_cvtepi32_ps(s0);
        t1 = _mm_cvtepi32_ps(s1);
        t2 = _mm_cvtepi32_ps(s2);
        t3 = _mm_cvtepi32_ps(s3);

        t0 = _mm_mul_ps(t0, scale);
        t1 = _mm_mul_ps(t1, scale);
        t2 = _mm_mul_ps(t2, scale);
        t3 = _mm_mul_ps(t3, scale);

        _MM_STOREX_PS(outdata, t0);
        outdata += 4;
        _MM_STOREX_PS(outdata, t1);
        outdata += 4;
        _MM_STOREX_PS(outdata, t2);
        outdata += 4;
        _MM_STOREX_PS(outdata, t3);
        outdata += 4;


        b0 = _mm_slli_epi32(z2, 12);     // [f6;  f4;  f2;  f0]
        b1 = _mm_and_si128(z2, ands);    // [f7;  f5;  f3;  f1]
        b2 = _mm_slli_epi32(z3, 12);     // [f14; f12; f10; f8]
        b3 = _mm_and_si128(z3, ands);    // [f15; f13; f11; f9]

        s0 = _mm_unpacklo_epi32(b0, b1); // [f3;  f2;  f2;  f0]
        s1 = _mm_unpackhi_epi32(b0, b1); // [f7;  f6;  f5;  f4]
        s2 = _mm_unpacklo_epi32(b2, b3); // [f11; f10; f9;  f8]
        s3 = _mm_unpackhi_epi32(b2, b3); // [f15; f14; f13; f12]

        t0 = _mm_cvtepi32_ps(s0);
        t1 = _mm_cvtepi32_ps(s1);
        t2 = _mm_cvtepi32_ps(s2);
        t3 = _mm_cvtepi32_ps(s3);

        t0 = _mm_mul_ps(t0, scale);
        t1 = _mm_mul_ps(t1, scale);
        t2 = _mm_mul_ps(t2, scale);
        t3 = _mm_mul_ps(t3, scale);

        _MM_STOREX_PS(outdata, t0);
        outdata += 4;
        _MM_STOREX_PS(outdata, t1);
        outdata += 4;
        _MM_STOREX_PS(outdata, t2);
        outdata += 4;
        _MM_STOREX_PS(outdata, t3);
        outdata += 4;
    }

    while(i >= 3)
    {
        uint8_t v0 = *(indata++);
        uint8_t v1 = *(indata++);
        uint8_t v2 = *(indata++);
        i -= 3;

        float a = (int16_t) (((uint16_t)v0 << 4) | ((uint16_t)v1 << 12));
        float b = (int16_t) (((uint16_t)v2 << 8) | (v1 & 0xf0));

        *(outdata++) = a * CONV_SCALE;
        *(outdata++) = b * CONV_SCALE;
    }

    if(i >= 2)
    {
        uint16_t v = *(const uint16_t*)indata;
        float a = (int16_t)(v << 4);
        *(outdata++) = a * CONV_SCALE;
        i -= 2;
    }

    if(i)
    {
        *outdata = 0;
    }
}

#undef TEMPLATE_FUNC_NAME
