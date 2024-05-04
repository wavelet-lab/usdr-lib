static
void TEMPLATE_FUNC_NAME(const int16_t *__restrict data,
                        const int16_t *__restrict conv,
                        int16_t *__restrict out,
                        unsigned count,
                        unsigned decim_bits,
                        unsigned flen)
{
    unsigned i, n;
    for (n = 0; n < count; n += (1 << decim_bits)) {
        __m128i acc = _mm_setzero_si128();

        for (i = 0; i < flen; i += 8) {
            __m128i c = _mm_load_si128((__m128i*)&conv[i]);
            __m128i d = _mm_loadu_si128((__m128i*)&data[n + i]);
            __m128i s = _mm_madd_epi16(d, c);
            acc = _mm_add_epi32(acc, s);
        }

        __m128i pshi = _mm_unpackhi_epi64(acc, acc);      // qs3 is2  .  qs3 is2
        __m128i pshm = _mm_add_epi32(acc, pshi);          //  x    x  . qss1 iss0

        __m128i nnnn = _mm_srli_epi64(pshm, 32);
        __m128i pshz = _mm_add_epi32(pshm, nnnn);

        __m128i pshnorm = _mm_srli_epi32(pshz, 15);

        out[(n >> decim_bits)] = _mm_extract_epi16(pshnorm, 0);
    }
}

#undef TEMPLATE_FUNC_NAME
