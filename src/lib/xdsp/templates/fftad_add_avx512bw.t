static
void TEMPLATE_FUNC_NAME(fft_acc_t* __restrict p, wvlt_fftwf_complex* __restrict d, unsigned fftsz)
{
    const __m512 fnoexp = _mm512_castsi512_ps(_mm512_set1_epi32(~(0xffu << 23)));
    const __m512 fexp0 = _mm512_castsi512_ps(_mm512_set1_epi32(127u << 23));
    const __m512i expcorr = _mm512_set1_epi32(127);
    const __m512 mine = _mm512_set1_ps(p->mine);

    const __m512i pidx0 = _mm512_set_epi32(30,28,26,24,22,20,18,16,  14,12,10,8,6,4,2,0);
    const __m512i pidx1 = _mm512_set_epi32(31,29,27,25,23,21,19,17,  15,13,11,9,7,5,3,1);

    for (unsigned i = 0; i < fftsz; i += 32)
    {
        __m512 e0 = _mm512_load_ps(&d[i +  0][0]);
        __m512 e1 = _mm512_load_ps(&d[i +  8][0]);
        __m512 e2 = _mm512_load_ps(&d[i + 16][0]);
        __m512 e3 = _mm512_load_ps(&d[i + 24][0]);

        __m512 acc_m0 = _mm512_load_ps(&p->f_mant[i +  0]);
        __m512 acc_m1 = _mm512_load_ps(&p->f_mant[i + 16]);

        __m512i acc_p0 = _mm512_load_si512((__m512i*)&p->f_pwr[i +  0]);
        __m512i acc_p1 = _mm512_load_si512((__m512i*)&p->f_pwr[i + 16]);

        __m512 p0 = _mm512_mul_ps(e0, e0);  // i0 q0 ... i3 q3
        __m512 p1 = _mm512_mul_ps(e1, e1);  // i4 q4 ... i7 q7
        __m512 p2 = _mm512_mul_ps(e2, e2);  // i8 q8 ... iB qB
        __m512 p3 = _mm512_mul_ps(e3, e3);  // iC qC ... iF qF

        __m512 pm0 = _mm512_permutex2var_ps(p0, pidx0, p1);
        __m512 pm1 = _mm512_permutex2var_ps(p0, pidx1, p1);
        __m512 pm2 = _mm512_permutex2var_ps(p2, pidx0, p3);
        __m512 pm3 = _mm512_permutex2var_ps(p2, pidx1, p3);

        __m512 en0 = _mm512_add_ps(pm0, pm1); // 0 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15
        __m512 en1 = _mm512_add_ps(pm2, pm3); // 16.....31

        __m512 enz0 = _mm512_add_ps(en0, mine);
        __m512 enz1 = _mm512_add_ps(en1, mine);

        __m512 zmpy0 = _mm512_mul_ps(enz0, acc_m0);
        __m512 zmpy1 = _mm512_mul_ps(enz1, acc_m1);

        __m512 zClearExp0 = _mm512_and_ps(fnoexp, zmpy0);
        __m512 zClearExp1 = _mm512_and_ps(fnoexp, zmpy1);

        __m512 z0 = _mm512_or_ps(zClearExp0, fexp0);
        __m512 z1 = _mm512_or_ps(zClearExp1, fexp0);

        __m512i az0 = _mm512_srli_epi32(_mm512_castps_si512(zmpy0), 23);
        __m512i az1 = _mm512_srli_epi32(_mm512_castps_si512(zmpy1), 23);

        __m512i azsum0 = _mm512_add_epi32(az0, acc_p0);
        __m512i azsum1 = _mm512_add_epi32(az1, acc_p1);

        __m512i azc0 = _mm512_sub_epi32(azsum0, expcorr);
        __m512i azc1 = _mm512_sub_epi32(azsum1, expcorr);

        _mm512_store_ps(&p->f_mant[i +  0], z0);
        _mm512_store_ps(&p->f_mant[i + 16], z1);

        _mm512_store_si512((__m512i*)&p->f_pwr[i +  0], azc0);
        _mm512_store_si512((__m512i*)&p->f_pwr[i + 16], azc1);
    }
}

#undef TEMPLATE_FUNC_NAME
