static
void TEMPLATE_FUNC_NAME(const int16_t *__restrict data,
                        const int16_t *__restrict conv,
                        int16_t *__restrict out,
                        unsigned count,
                        unsigned decim_bits,
                        unsigned flen)
{
    unsigned i, n;
    for (n = 0; n < count; n += (2 << decim_bits)) {
        __m128i acc[2] = { _mm_setzero_si128(), _mm_setzero_si128() };

        for (i = 0; i < flen; i += 8) {
            __m128i c = _mm_load_si128((__m128i*)&conv[i]);
            __m128i d0 = _mm_loadu_si128((__m128i*)&data[n + 2 * i + 0]);  // q3 i3 q2 i2 . q1 i1 q0 i0
            __m128i d1 = _mm_loadu_si128((__m128i*)&data[n + 2 * i + 8]);  // q7 i7 q6 i6 . q5 i5 q4 i4

            __m128i phm0 = _mm_shuffle_epi8(d0, _mm_set_epi8(15, 14, 11, 10, 7, 6, 3, 2, 13, 12, 9, 8, 5, 4, 1, 0));
            __m128i phm1 = _mm_shuffle_epi8(d1, _mm_set_epi8(15, 14, 11, 10, 7, 6, 3, 2, 13, 12, 9, 8, 5, 4, 1, 0));

            __m128i vi  = _mm_unpacklo_epi64(phm0, phm1); // i vec
            __m128i vq  = _mm_unpackhi_epi64(phm0, phm1); // q vec

            __m128i dsi = _mm_madd_epi16(vi, c);          //  qs1 is1 . qs0 is0
            __m128i dsq = _mm_madd_epi16(vq, c);          //  qs3 is3 . qs2 is2

            acc[0] = _mm_add_epi32(acc[0], dsi);
            acc[1] = _mm_add_epi32(acc[1], dsq);
        }

        __m128i ps0 = _mm_unpacklo_epi32(acc[0], acc[1]); // qs1 is1 qs0 is0
        __m128i ps1 = _mm_unpackhi_epi32(acc[0], acc[1]); // qs3 is3 qs2 is2

        __m128i psh = _mm_add_epi32(ps0, ps1);            // qss1 iss1 . qss0 iss0
        __m128i pshi = _mm_unpackhi_epi64(psh, psh);      // qss1 iss1 . qss1 iss1
        __m128i pshm = _mm_add_epi32(psh, pshi);          //  x    x   . qsss isss
        __m128i pshnorm = _mm_srli_epi32(pshm, 15);       // x x . x x . x q . x i

        out[(n >> decim_bits) + 0] = _mm_extract_epi16(pshnorm, 0);
        out[(n >> decim_bits) + 1] = _mm_extract_epi16(pshnorm, 2);
    }
}

#undef TEMPLATE_FUNC_NAME
