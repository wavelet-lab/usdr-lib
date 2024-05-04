static
void TEMPLATE_FUNC_NAME(fft_acc_t* __restrict p, wvlt_fftwf_complex* __restrict d, unsigned fftsz)
{
    const __m256 fnoexp = _mm256_castsi256_ps(_mm256_set1_epi32(~(0xffu << 23)));
    const __m256 fexp0 = _mm256_castsi256_ps(_mm256_set1_epi32(127u << 23));
    const __m256i expcorr = _mm256_set1_epi32(127);

    __m256 mine = _mm256_set1_ps(p->mine);

    for (unsigned i = 0; i < fftsz; i += 16) {
        __m256 e0 = _mm256_load_ps(&d[i + 0][0]);
        __m256 e1 = _mm256_load_ps(&d[i + 4][0]);
        __m256 e2 = _mm256_load_ps(&d[i + 8][0]);
        __m256 e3 = _mm256_load_ps(&d[i + 12][0]);

        __m256 acc_m0 = _mm256_load_ps(&p->f_mant[i + 0]);
        __m256 acc_m1 = _mm256_load_ps(&p->f_mant[i + 8]);

        __m256i acc_p0 = _mm256_load_si256((__m256i*)&p->f_pwr[i + 0]);
        __m256i acc_p1 = _mm256_load_si256((__m256i*)&p->f_pwr[i + 8]);

        __m256 p0 = _mm256_mul_ps(e0, e0);  // i0 q0 ... i3 q3
        __m256 p1 = _mm256_mul_ps(e1, e1);  // i4 q4 ... i7 q7
        __m256 p2 = _mm256_mul_ps(e2, e2);  // i8 q8 ... iB qB
        __m256 p3 = _mm256_mul_ps(e3, e3);  // iC qC ... iF qF

        __m256 en0 = _mm256_hadd_ps(p0, p1); // pwr{ 0 1 4 5 2 3 6 7 }
        __m256 en1 = _mm256_hadd_ps(p2, p3); // pwr{ 8 9 C D A B E F }
        __m256 enz0 = _mm256_add_ps(en0, mine);
        __m256 enz1 = _mm256_add_ps(en1, mine);

        __m256 zmpy0 = _mm256_mul_ps(enz0, acc_m0);
        __m256 zmpy1 = _mm256_mul_ps(enz1, acc_m1);

        __m256 zClearExp0 = _mm256_and_ps(fnoexp, zmpy0);
        __m256 zClearExp1 = _mm256_and_ps(fnoexp, zmpy1);

        __m256 z0 = _mm256_or_ps(zClearExp0, fexp0);
        __m256 z1 = _mm256_or_ps(zClearExp1, fexp0);

        __m256i az0 = _mm256_srli_epi32(_mm256_castps_si256(zmpy0), 23);
        __m256i az1 = _mm256_srli_epi32(_mm256_castps_si256(zmpy1), 23);

        __m256i azsum0 = _mm256_add_epi32(az0, acc_p0);
        __m256i azsum1 = _mm256_add_epi32(az1, acc_p1);

        __m256i azc0 = _mm256_sub_epi32(azsum0, expcorr);
        __m256i azc1 = _mm256_sub_epi32(azsum1, expcorr);

        _mm256_store_ps(&p->f_mant[i + 0], z0);
        _mm256_store_ps(&p->f_mant[i + 8], z1);

        _mm256_store_si256((__m256i*)&p->f_pwr[i + 0], azc0);
        _mm256_store_si256((__m256i*)&p->f_pwr[i + 8], azc1);
    }
}

#undef TEMPLATE_FUNC_NAME
