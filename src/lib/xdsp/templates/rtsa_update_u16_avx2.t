static
void TEMPLATE_FUNC_NAME(wvlt_fftwf_complex* __restrict in, unsigned fft_size,
                        fft_rtsa_data_t* __restrict rtsa_data,
                        float fcale_mpy, float mine, float corr, fft_diap_t diap)
{
    // Attention please!
    // rtsa_depth should be multiple to 32/sizeof(rtsa_pwr_t) here!
    // It will crash otherwise, due to aligning issues!
    //

#include "rtsa_update_u16_avx2.inc"

#ifdef USE_POLYLOG2
    WVLT_POLYLOG2_DECL_CONSTS;
    wvlt_log2f_fn_t wvlt_log2f_fn = wvlt_polylog2f;
#else
    const __m256 log2_mul      = _mm256_set1_ps(WVLT_FASTLOG2_MUL);
    const __m256 log2_sub      = _mm256_set1_ps(WVLT_FASTLOG2_SUB);
    wvlt_log2f_fn_t wvlt_log2f_fn = wvlt_fastlog2;
#endif

    const fft_rtsa_settings_t * st = &rtsa_data->settings;
    const unsigned rtsa_depth = st->rtsa_depth;
    const float charge_rate = (float)st->raise_coef * st->divs_for_dB / st->charging_frame;
    const unsigned decay_rate_pw2 = (unsigned)(wvlt_log2f_fn(st->charging_frame * st->decay_coef) + 0.5);

    const __m256 v_scale_mpy   = _mm256_set1_ps(fcale_mpy * (float)st->divs_for_dB);
    const __m256 v_mine        = _mm256_set1_ps(mine);
    const __m256 v_corr        = _mm256_set1_ps((corr - (float)st->upper_pwr_bound) * (float)st->divs_for_dB);

    const __m256 sign_bit      = _mm256_set1_ps(-0.0f);
    const __m256i v_depth      = _mm256_set1_epi32((int32_t)rtsa_depth);
    const __m256 max_ind       = _mm256_set1_ps((float)(rtsa_depth - 1) - 0.5f);
    const __m256 ch_rate       = _mm256_set1_ps(charge_rate);
    const __m256 ch_norm_coef  = _mm256_set1_ps(CHARGE_NORM_COEF);
    const __m256 f_ones        = _mm256_set1_ps(1.0f);
    const __m256i v_maxcharge  = _mm256_set1_epi32(MAX_RTSA_PWR);

    const unsigned discharge_add = ((unsigned)(DISCHARGE_NORM_COEF) >> decay_rate_pw2);
    const __m256i dch_add_coef = _mm256_set1_epi16((uint16_t)discharge_add);
    const __m128i dch_rshift   = _mm_set_epi64x(0, decay_rate_pw2);

    typedef int32_t v8si __attribute__ ((vector_size (32)));
    union u_v8si { __m256i vect; v8si arr; };
    typedef union u_v8si u_v8si_t;

    typedef uint16_t v16si __attribute__ ((vector_size (32)));
    union u_v16si { __m256i vect; v16si arr; };
    typedef union u_v16si u_v16si_t;

    const unsigned rtsa_depth_bz = rtsa_depth * sizeof(rtsa_pwr_t);

    for (unsigned i = diap.from; i < diap.to; i += 16)
    {
#if 1
        // load 8*4 = 32 floats = 16 complex pairs
        //
        __m256 e0 = _mm256_load_ps(&in[i +  0][0]);
        __m256 e1 = _mm256_load_ps(&in[i +  4][0]);
        __m256 e2 = _mm256_load_ps(&in[i +  8][0]);
        __m256 e3 = _mm256_load_ps(&in[i + 12][0]);

        // mul, sqr
        //
        __m256 eq0 = _mm256_mul_ps(e0, e0);
        __m256 eq1 = _mm256_mul_ps(e1, e1);
        __m256 eq2 = _mm256_mul_ps(e2, e2);
        __m256 eq3 = _mm256_mul_ps(e3, e3);

        // sum them
        //
        __m256 sum0s = _mm256_hadd_ps(eq0, eq1);  // pwr{ 0 1 4 5 2 3 6 7 }
        __m256 sum1s = _mm256_hadd_ps(eq2, eq3);  // pwr{ 8 9 C D A B E F }

        // add mine
        //
        __m256 summ0 = _mm256_add_ps(sum0s, v_mine);
        __m256 summ1 = _mm256_add_ps(sum1s, v_mine);

#ifdef USE_POLYLOG2
        __m256 l2_res0, l2_res1;
        WVLT_POLYLOG2F8(summ0, l2_res0);
        WVLT_POLYLOG2F8(summ1, l2_res1);
#else
        // fasterlog2
        //
        __m256 summ0_ = _mm256_cvtepi32_ps(_mm256_castps_si256(summ0));
        __m256 summ1_ = _mm256_cvtepi32_ps(_mm256_castps_si256(summ1));
        __m256 l2_res0 = _mm256_fmsub_ps(summ0_, log2_mul, log2_sub);
        __m256 l2_res1 = _mm256_fmsub_ps(summ1_, log2_mul, log2_sub);
#endif
        // add scale & corr
        __m256 pwr0 = _mm256_fmadd_ps(l2_res0, v_scale_mpy, v_corr);
        __m256 pwr1 = _mm256_fmadd_ps(l2_res1, v_scale_mpy, v_corr);

        // drop sign
        //
        __m256 p0 = _mm256_andnot_ps(sign_bit, pwr0);
        __m256 p1 = _mm256_andnot_ps(sign_bit, pwr1);

        // normalize
        //
        __m256 pn0 = _mm256_min_ps(p0, max_ind);
        __m256 pn1 = _mm256_min_ps(p1, max_ind);

        // low bound
        //
        __m256 lo0 = _mm256_round_ps(pn0, _MM_FROUND_TO_ZERO | _MM_FROUND_NO_EXC);
        __m256 lo1 = _mm256_round_ps(pn1, _MM_FROUND_TO_ZERO | _MM_FROUND_NO_EXC);

        // high bound
        //
        __m256 hi0 = _mm256_add_ps(lo0, f_ones);
        __m256 hi1 = _mm256_add_ps(lo1, f_ones);

        // load charge cells
        //
        const __m256i fft_offs0 =
            _mm256_mullo_epi32(_mm256_set_epi32(i + 7,  i + 6,  i +  3, i +  2, i +  5, i +  4, i + 1, i + 0), v_depth);
        const __m256i fft_offs1 =
            _mm256_mullo_epi32(_mm256_set_epi32(i + 15, i + 14, i + 11, i + 10, i + 13, i + 12, i + 9, i + 8), v_depth);

        u_v8si_t
            lo0_offs = { _mm256_add_epi32(fft_offs0, _mm256_cvtps_epi32(lo0)) },
            lo1_offs = { _mm256_add_epi32(fft_offs1, _mm256_cvtps_epi32(lo1)) },
            hi0_offs = { _mm256_add_epi32(fft_offs0, _mm256_cvtps_epi32(hi0)) },
            hi1_offs = { _mm256_add_epi32(fft_offs1, _mm256_cvtps_epi32(hi1)) };

        u_v16si_t pwri_lo0 = {_mm256_setzero_si256()};
        u_v16si_t pwri_lo1 = {_mm256_setzero_si256()};
        u_v16si_t pwri_hi0 = {_mm256_setzero_si256()};
        u_v16si_t pwri_hi1 = {_mm256_setzero_si256()};

        for(unsigned j = 0, j2 = 0; j < 8; ++j, j2 += 2)
        {
            pwri_lo0.arr[j2] = *(rtsa_data->pwr + lo0_offs.arr[j]);
            pwri_lo1.arr[j2] = *(rtsa_data->pwr + lo1_offs.arr[j]);
            pwri_hi0.arr[j2] = *(rtsa_data->pwr + hi0_offs.arr[j]);
            pwri_hi1.arr[j2] = *(rtsa_data->pwr + hi1_offs.arr[j]);
        }

        __m256 pwr_lo0 = _mm256_cvtepi32_ps(pwri_lo0.vect);
        __m256 pwr_lo1 = _mm256_cvtepi32_ps(pwri_lo1.vect);
        __m256 pwr_hi0 = _mm256_cvtepi32_ps(pwri_hi0.vect);
        __m256 pwr_hi1 = _mm256_cvtepi32_ps(pwri_hi1.vect);

        // calc charge rates
        //
        __m256 charge_hi0 = _mm256_mul_ps( _mm256_sub_ps(pn0, lo0), ch_rate );
        __m256 charge_hi1 = _mm256_mul_ps( _mm256_sub_ps(pn1, lo1), ch_rate );
        __m256 charge_lo0 = _mm256_mul_ps( _mm256_sub_ps(hi0, pn0), ch_rate );
        __m256 charge_lo1 = _mm256_mul_ps( _mm256_sub_ps(hi1, pn1), ch_rate );

        // charge
        //
        __m256i cdelta_lo0 = _mm256_cvtps_epi32(_mm256_mul_ps(_mm256_sub_ps(ch_norm_coef, pwr_lo0), charge_lo0));
        __m256i cdelta_lo1 = _mm256_cvtps_epi32(_mm256_mul_ps(_mm256_sub_ps(ch_norm_coef, pwr_lo1), charge_lo1));
        __m256i cdelta_hi0 = _mm256_cvtps_epi32(_mm256_mul_ps(_mm256_sub_ps(ch_norm_coef, pwr_hi0), charge_hi0));
        __m256i cdelta_hi1 = _mm256_cvtps_epi32(_mm256_mul_ps(_mm256_sub_ps(ch_norm_coef, pwr_hi1), charge_hi1));

        pwri_lo0.vect = _mm256_min_epu32(_mm256_add_epi32(pwri_lo0.vect, cdelta_lo0), v_maxcharge);
        pwri_lo1.vect = _mm256_min_epu32(_mm256_add_epi32(pwri_lo1.vect, cdelta_lo1), v_maxcharge);
        pwri_hi0.vect = _mm256_min_epu32(_mm256_add_epi32(pwri_hi0.vect, cdelta_hi0), v_maxcharge);
        pwri_hi1.vect = _mm256_min_epu32(_mm256_add_epi32(pwri_hi1.vect, cdelta_hi1), v_maxcharge);

        // store charged
        //
        for( unsigned j = 0, j2 = 0; j < 8; ++j, j2 += 2)
        {
            *(rtsa_data->pwr + lo0_offs.arr[j]) = pwri_lo0.arr[j2];
            *(rtsa_data->pwr + lo1_offs.arr[j]) = pwri_lo1.arr[j2];
            *(rtsa_data->pwr + hi0_offs.arr[j]) = pwri_hi0.arr[j2];
            *(rtsa_data->pwr + hi1_offs.arr[j]) = pwri_hi1.arr[j2];
        }
#endif
        // discharge all
        // note - we will discharge cells in the [i, i+16) fft band because those pages are already loaded to cache
        //
        RTSA_U16_DISCHARGE(16);
    }
}

#undef TEMPLATE_FUNC_NAME
