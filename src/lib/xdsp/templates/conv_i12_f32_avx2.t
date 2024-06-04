static
void TEMPLATE_FUNC_NAME(const void *__restrict indata_p,
                        unsigned indatabsz,
                        void *__restrict outdata_p,
                        unsigned outdatabsz)
{
    unsigned i = indatabsz;
    /* 12 bits -> 32 bits  =>  3 -> 8   */
    if ((outdatabsz * 3 / 8) < i)
        i = (outdatabsz * 3 / 8);

    const __m256i *in256 = (const __m256i*)indata_p;
    float* out = (float*)outdata_p;

    const __m256i zeros = _mm256_setzero_si256();
    const __m256i shl_ctrl = _mm256_set_epi64x(64,32,16,0);
    const __m256i shr_ctrl = _mm256_set_epi64x(0,16,32,48);
    const __m256i mask0 = _mm256_set1_epi64x(0xfff0000000000000);
    const __m256i mask1 = _mm256_set1_epi64x(0x0000fff000000000);
    const __m256i mask2 = _mm256_set1_epi64x(0x00000000fff00000);
    const __m256i mask3 = _mm256_set1_epi64x(0x000000000000fff0);
    const __m256  scale = _mm256_set1_ps(SCALE2);

    __m256i v0, v1, v2, v3;
    __m256i a, b, c, d;
    __m256i r0, r1;
    __m256i result;
    __m256i d0, i0, i1;
    __m256  f0, f1, f2, f3, f4, f5, f6, f7;

    typedef uint64_t v8u64 __attribute__ ((vector_size (32)));
    union u_v8u64 { __m256i vect; v8u64 arr; };
    typedef union u_v8u64 u_v8u64_t;

#define CONVERT_I12_F32_BLOCK(reg, dst0, dst1) \
    v0 = _mm256_sllv_epi64(reg, shl_ctrl);                           /* 1 */ \
    v1 = _mm256_srlv_epi64(reg, shr_ctrl);                           /* 1 */ \
    v2 = _mm256_permute4x64_epi64(v1, _MM_SHUFFLE(2,1,0,3));         /* 3 */ \
    v3 = _mm256_or_si256(v0, v2);                                    /* 1 */ \
    \
    a = _mm256_and_si256(_mm256_slli_epi64(v3, 16), mask0);      /* 1 + 1 */ \
    b = _mm256_and_si256(_mm256_slli_epi64(v3, 12), mask1);      /* 1 + 1 */ \
    c = _mm256_and_si256(_mm256_slli_epi64(v3,  8), mask2);      /* 1 + 1 */ \
    d = _mm256_and_si256(_mm256_slli_epi64(v3,  4), mask3);      /* 1 + 1 */ \
    \
    r0 = _mm256_or_si256(a, b);                                      /* 1 */ \
    r1 = _mm256_or_si256(c, d);                                      /* 1 */ \
    result = _mm256_or_si256(r0, r1);                                /* 1 */ \
    \
    d0 = _mm256_permute4x64_epi64(result, _MM_SHUFFLE(3, 1, 2, 0));  /* 3 */ \
    i0 = _mm256_unpacklo_epi16(zeros,d0);                            /* 1 */ \
    i1 = _mm256_unpackhi_epi16(zeros,d0);                            /* 1 */ \
    \
    dst0 = _mm256_cvtepi32_ps(i0);                                   /* 4 */ \
    dst1 = _mm256_cvtepi32_ps(i1);                                   /* 4 */ \
    \
    dst0 = _mm256_mul_ps(dst0, scale);                               /* 4 */ \
    dst1 = _mm256_mul_ps(dst1, scale);                               /* 4 */ \

// CONVERT_I12_F32_BLOCK end                                       lat  38


    for (; i >= 96; i -= 96)
    {
        u_v8u64_t x0 = {_mm256_load_si256(in256++)};                            // 7
        u_v8u64_t x1 = {_mm256_load_si256(in256++)};                            // 7
        u_v8u64_t x2 = {_mm256_load_si256(in256++)};                            // 7

        __m256i y0  = _mm256_set_epi64x(0, x0.arr[2], x0.arr[1], x0.arr[0]);
        __m256i y1  = _mm256_set_epi64x(0, x1.arr[1], x1.arr[0], x0.arr[3]);
        __m256i y2  = _mm256_set_epi64x(0, x2.arr[0], x1.arr[3], x1.arr[2]);
        __m256i y3  = _mm256_set_epi64x(0, x2.arr[3], x2.arr[2], x2.arr[1]);

        CONVERT_I12_F32_BLOCK(y0, f0, f1);
        CONVERT_I12_F32_BLOCK(y1, f2, f3);
        CONVERT_I12_F32_BLOCK(y2, f4, f5);
        CONVERT_I12_F32_BLOCK(y3, f6, f7);                                       // 38*4 = 152

        _MM256_STOREX_PS(out, f0); out += 8;
        _MM256_STOREX_PS(out, f1); out += 8;
        _MM256_STOREX_PS(out, f2); out += 8;
        _MM256_STOREX_PS(out, f3); out += 8;
        _MM256_STOREX_PS(out, f4); out += 8;
        _MM256_STOREX_PS(out, f5); out += 8;
        _MM256_STOREX_PS(out, f6); out += 8;
        _MM256_STOREX_PS(out, f7); out += 8;                                    // 8
    }                                                                           // lat 181 = 2.83 per f32

#undef CONVERT_I12_F32_BLOCK

    const uint8_t *indata = (const uint8_t*)in256;

    while(i >= 3)
    {
        uint8_t v0 = *(indata++);
        uint8_t v1 = *(indata++);
        uint8_t v2 = *(indata++);
        i -= 3;

        float a = (int16_t) (((uint16_t)v0 << 4) | ((uint16_t)v1 << 12));
        float b = (int16_t) (((uint16_t)v2 << 8) | (v1 & 0xf0));

        *(out++) = a * CONV_SCALE;
        *(out++) = b * CONV_SCALE;
    }

    if(i >= 2)
    {
        uint16_t v = *(const uint16_t*)indata;
        float a = (int16_t)(v << 4);
        *(out++) = a * CONV_SCALE;
        i -= 2;
    }

    if(i)
    {
        *out = 0;
    }
}
#undef TEMPLATE_FUNC_NAME
