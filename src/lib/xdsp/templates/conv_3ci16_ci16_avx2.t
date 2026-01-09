
static
void TEMPLATE_FUNC_NAME(const void *__restrict indata_0_p,
                        const void *__restrict indata_1_p,
                        const void *__restrict indata_2_p,
                        unsigned indatabsz,
                        void *__restrict outdata_p,
                        unsigned outdatabsz)
{

#if 0
    typedef uint16_t v16si __attribute__ ((vector_size (32)));
    union u_v16si { __m256i vect; v16si arr; };
    typedef union u_v16si u_v16si_t;

#define RPRINT(reg) \
{ \
    u_v16si_t p = {reg}; \
    for(int i = 15; i >= 0; --i) \
    { \
        printf("%2d ", p.arr[i]); \
    } \
    printf("\n"); \
}

#define RPRINT3(title, r0, r1, r2) \
{ \
    printf("%s:\n", title); \
    RPRINT(r0); \
    RPRINT(r1); \
    RPRINT(r2); \
}

#else
#define RPRINT3(title, r0, r1, r2) {}
#endif

    unsigned i = indatabsz;
    if ((outdatabsz) < i)
        i = (outdatabsz);

    const int16_t* indata_0 = (int16_t*)indata_0_p;
    const int16_t* indata_1 = (int16_t*)indata_1_p;
    const int16_t* indata_2 = (int16_t*)indata_2_p;
    int16_t* outdata = (int16_t*)outdata_p;

    //
    const __m256i mask0 = _mm256_set_epi32(-1, 0, -1, 0, -1, 0, -1, 0);
    const __m256i mask1 = _mm256_set_epi32(0, -1, 0, -1, 0, -1, 0, -1);

    for (; i >= 32 * 3; i -= 32 * 3)
    {
        __m256i r0 = _mm256_load_si256((__m256i*)indata_0);
        __m256i r1 = _mm256_load_si256((__m256i*)indata_1);
        __m256i r2 = _mm256_load_si256((__m256i*)indata_2);

        indata_0 += 16;
        indata_1 += 16;
        indata_2 += 16;

        RPRINT3("init",r0,r1,r2);

        //(0)
        __m256d ulo, uhi;
        ulo = _mm256_castsi256_pd(_mm256_unpacklo_epi32(r0, r1));
        uhi = _mm256_castsi256_pd(_mm256_unpackhi_epi32(r0, r1));
        __m256i rs0 = _mm256_castpd_si256(_mm256_shuffle_pd(ulo, uhi, 0b0000));

        ulo = _mm256_castsi256_pd(_mm256_unpacklo_epi32(r1, r2));
        uhi = _mm256_castsi256_pd(_mm256_unpackhi_epi32(r1, r2));
        __m256i rs2 = _mm256_castpd_si256(_mm256_shuffle_pd(ulo, uhi, 0b1111));

        __m256i rs1 = _mm256_or_si256(_mm256_and_si256(r0, mask0), _mm256_and_si256(r2, mask1));

        RPRINT3("(0)",rs0,rs1,rs2);

        //(1)
        __m256i z0 = _mm256_castpd_si256(_mm256_shuffle_pd(_mm256_castsi256_pd(rs0), _mm256_castsi256_pd(rs1), 0b0000));
        __m256i z1 = _mm256_castpd_si256(_mm256_shuffle_pd(_mm256_castsi256_pd(rs0), _mm256_castsi256_pd(rs1), 0b1111));
        __m256i z2 = rs2;

        __m256i zz0 = z0;
        __m256i zz1 = _mm256_castpd_si256(_mm256_shuffle_pd(_mm256_castsi256_pd(z1), _mm256_castsi256_pd(z2), 0b0000));
        __m256i zz2 = _mm256_castpd_si256(_mm256_shuffle_pd(_mm256_castsi256_pd(z1), _mm256_castsi256_pd(z2), 0b1111));

        zz1 = _mm256_shuffle_epi32(zz1, _MM_SHUFFLE(1,0,3,2));

        RPRINT3("(1)",zz0,zz1,zz2);

        //(2)
        __m256i x0 = _mm256_permute2x128_si256(zz0, zz1, 0b00100000);
        __m256i x1 = _mm256_permute2x128_si256(zz0, zz1, 0b00110001);
        __m256i x2 = zz2;

        __m256i xx0 = x0;
        __m256i xx1 = _mm256_permute2x128_si256(x1, x2, 0b00100000);
        __m256i xx2 = _mm256_permute2x128_si256(x1, x2, 0b00110001);

        xx1 = _mm256_permute2x128_si256(xx1, xx1, 0x1);

        RPRINT3("(2)",xx0,xx1,xx2);

        _mm256_storeu_si256((__m256i*)(outdata +  0), xx0);
        _mm256_storeu_si256((__m256i*)(outdata + 16), xx1);
        _mm256_storeu_si256((__m256i*)(outdata + 32), xx2);
        outdata += 48;
    }

    #include "conv_3ci16_ci16_generic.inc"
}

#undef TEMPLATE_FUNC_NAME
