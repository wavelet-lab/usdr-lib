static
void TEMPLATE_FUNC_NAME(const int16_t *__restrict data,
                        const int16_t *__restrict conv,
                        int16_t *__restrict out,
                        unsigned count,
                        unsigned decim_bits,
                        unsigned flen)
{
    unsigned i, n;
    __m256i msk = _mm256_set_epi8(13, 12, 15, 14, 9, 8, 11, 10, 5, 4, 7, 6, 1, 0, 3, 2,
                                  13, 12, 15, 14, 9, 8, 11, 10, 5, 4, 7, 6, 1, 0, 3, 2);
    for (n = 0; n < count; n += (2 << decim_bits)) {
        __m256i acc[2] = { _mm256_setzero_si256(), _mm256_setzero_si256() };

        for (i = 0; i < flen; i += 16) {
            __m256i c = _mm256_load_si256((__m256i*)&conv[i]);
            __m256i d0 = _mm256_loadu_si256((__m256i*)&data[n + 2 * i + 0]);  // q7 i7 q6 i6 q5 i5 q4 i4 . q3 i3 q2 i2 q1 i1 q0 i0
            __m256i d1 = _mm256_loadu_si256((__m256i*)&data[n + 2 * i + 16]); // qF iF qE iE qD iD qC iC . qB iB qA iA q9 i9 q8 i8

            __m256i d0sh = _mm256_shuffle_epi8(d0, msk);                      // i7 q7 i6 q6 i5 q5 i4 q4 . i3 q3 i2 q2 i1 q1 i0 q0
            __m256i phmX = _mm256_blend_epi16(d0sh, d1, 0xAA);                // qF q7 qE q6 qD q5 qC q4   qB q3 qA q2 q9 q1 q8 q0
            __m256i phm0 = _mm256_blend_epi16(d0sh, d1, 0x55);                // i7 iF i6 iE i5 iD i4 iC   i3 iB i2 iA i1 i9 i0 i8
            __m256i phm1 = _mm256_shuffle_epi8(phmX, msk);                    // q7 qF q6 qE q5 qD q4 qC   q3 qB q2 qA q1 q9 q0 q8

            __m256i dsi = _mm256_madd_epi16(phm0, c); // is7 is6 is5 is4 is3 is2 is1 is0
            __m256i dsq = _mm256_madd_epi16(phm1, c);

            acc[0] = _mm256_add_epi32(acc[0], dsi);
            acc[1] = _mm256_add_epi32(acc[1], dsq);
        }

        __m256i ps0 = _mm256_unpacklo_epi32(acc[0], acc[1]); // qs5 is5 qs4 is4  qs1 is1 qs0 is0
        __m256i ps1 = _mm256_unpackhi_epi32(acc[0], acc[1]); // qs7 is7 qs5 is5  qs3 is3 qs2 is2
        __m256i psh = _mm256_add_epi32(ps0, ps1);            // qss3 iss3 . qss2 iss2 . qss1 iss1 . qss0 iss0
        __m256i pshi = _mm256_permute4x64_epi64 (psh, _MM_SHUFFLE(3, 2, 3, 2));  //     qss3 iss3 . qss2 iss2
        __m256i pshm = _mm256_add_epi32(psh, pshi);                              //     qsN1 isN1 . qsN0 isN0
        __m256i pshl = _mm256_unpackhi_epi64(pshm, pshm);
        __m256i pshk = _mm256_add_epi32(pshl, pshm);
        __m256i pshnorm = _mm256_srli_epi32(pshk, 13);                           //     x x . x x . x q . x i

        out[(n >> decim_bits) + 0] = _mm256_extract_epi16(pshnorm, 0);
        out[(n >> decim_bits) + 1] = _mm256_extract_epi16(pshnorm, 2);
    }
}

#undef TEMPLATE_FUNC_NAME
