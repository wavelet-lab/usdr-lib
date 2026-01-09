static
void TEMPLATE_FUNC_NAME(const void *__restrict indata,
                        unsigned indatabsz,
                        void *__restrict outdata_0_p,
                        void *__restrict outdata_1_p,
                        void *__restrict outdata_2_p,
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

    int16_t* outdata_0 = (int16_t*)outdata_0_p;
    int16_t* outdata_1 = (int16_t*)outdata_1_p;
    int16_t* outdata_2 = (int16_t*)outdata_2_p;

    const __m256i mask0 = _mm256_set_epi32(-1, 0, -1, 0, -1, 0, -1, 0);
    const __m256i mask1 = _mm256_set_epi32(0, -1, 0, -1, 0, -1, 0, -1);
    const __m256i * inptr = (const __m256i*)indata;

/*
r0: HGFEDCBA     HGFEDCBA     3210DCBA     3210DCBA     7610HGBA     7610HGBA     9630JGDA
r1: 3210LKJI -0- LKJI3210 -1- 7654HGFE -2- 5476FEHG -3- 9832JIDC -4- 8923IJCD -5- a741KHEB
r2: ba987654     ba987654     ba98LKJI     ba98LKJI     ba54LKFE     ba54LKFE     b852LIFC
*/

    for (; i >= 32 * 3; i -= 32 * 3)
    {
        __m256i r0 = _mm256_loadu_si256(inptr++);
        __m256i r1 = _mm256_loadu_si256(inptr++);
        __m256i r2 = _mm256_loadu_si256(inptr++);

        RPRINT3("init",r0,r1,r2);

        //(0)
        r1 = _mm256_permute2x128_si256(r1, r1, 0x1);
        RPRINT3("(0)",r0,r1,r2);


        //(1)
        __m256i x0 = _mm256_permute2x128_si256(r0, r1, 0b00100000);
        __m256i x1 = _mm256_permute2x128_si256(r0, r1, 0b00110001);
        __m256i x2 = r2;

        __m256i xx0 = x0;
        __m256i xx1 = _mm256_permute2x128_si256(x1, x2, 0b00100000);
        __m256i xx2 = _mm256_permute2x128_si256(x1, x2, 0b00110001);
        RPRINT3("(1)",xx0,xx1,xx2);


        //(2)
        xx1 = _mm256_shuffle_epi32(xx1, _MM_SHUFFLE(1,0,3,2));
        RPRINT3("(2)",xx0,xx1,xx2);


        //(3)
        __m256i z0 = _mm256_castpd_si256(_mm256_shuffle_pd(_mm256_castsi256_pd(xx0), _mm256_castsi256_pd(xx1), 0b0000));
        __m256i z1 = _mm256_castpd_si256(_mm256_shuffle_pd(_mm256_castsi256_pd(xx0), _mm256_castsi256_pd(xx1), 0b1111));
        __m256i z2 = xx2;

        __m256i zz0 = z0;
        __m256i zz1 = _mm256_castpd_si256(_mm256_shuffle_pd(_mm256_castsi256_pd(z1), _mm256_castsi256_pd(z2), 0b0000));
        __m256i zz2 = _mm256_castpd_si256(_mm256_shuffle_pd(_mm256_castsi256_pd(z1), _mm256_castsi256_pd(z2), 0b1111));
        RPRINT3("(3)",zz0,zz1,zz2);


        //(4)
        zz1 = _mm256_shuffle_epi32(zz1, _MM_SHUFFLE(2,3,0,1));
        RPRINT3("(4)",zz0,zz1,zz2);

        //(5)

        __m256d ulo, uhi;
        ulo = _mm256_castsi256_pd(_mm256_unpacklo_epi32(zz0, zz1));
        uhi = _mm256_castsi256_pd(_mm256_unpackhi_epi32(zz0, zz1));
        __m256i rs0 = _mm256_castpd_si256(_mm256_shuffle_pd(ulo, uhi, 0b0000));

        ulo = _mm256_castsi256_pd(_mm256_unpacklo_epi32(zz1, zz2));
        uhi = _mm256_castsi256_pd(_mm256_unpackhi_epi32(zz1, zz2));
        __m256i rs2 = _mm256_castpd_si256(_mm256_shuffle_pd(ulo, uhi, 0b1111));

        __m256i rs1 = _mm256_or_si256(_mm256_and_si256(zz0, mask0), _mm256_and_si256(zz2, mask1));
        rs1 = _mm256_shuffle_epi32(rs1, _MM_SHUFFLE(2,3,0,1));
        RPRINT3("(5)",rs0,rs1,rs2);

        _mm256_store_si256((__m256i*)outdata_0, rs0);
        _mm256_store_si256((__m256i*)outdata_1, rs1);
        _mm256_store_si256((__m256i*)outdata_2, rs2);

        outdata_0 += 16;
        outdata_1 += 16;
        outdata_2 += 16;
    }

    const uint32_t *ld = (const uint32_t *)inptr;
    #include "conv_ci16_3ci16_generic.inc"
}

#undef TEMPLATE_FUNC_NAME
