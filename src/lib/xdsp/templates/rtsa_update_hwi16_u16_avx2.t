static
void TEMPLATE_FUNC_NAME(uint16_t* __restrict in, unsigned fft_size,
                        fft_rtsa_data_t* __restrict rtsa_data,
                        float scale, float corr, fft_diap_t diap)
{
    // Attention please!
    // rtsa_depth should be multiple to 32/sizeof(rtsa_pwr_t) here!
    // It will crash otherwise, due to aligning issues!
    //

#include "rtsa_update_u16_avx2.inc"

#ifdef USE_POLYLOG2
    wvlt_log2f_fn_t wvlt_log2f_fn = wvlt_polylog2f;
#else
    wvlt_log2f_fn_t wvlt_log2f_fn = wvlt_fastlog2;
#endif

    const fft_rtsa_settings_t * st = &rtsa_data->settings;
    const unsigned rtsa_depth = st->rtsa_depth;
    const float charge_rate = (float)st->raise_coef * st->divs_for_dB / st->charging_frame;
    const unsigned decay_rate_pw2 = (unsigned)(wvlt_log2f_fn(st->charging_frame * st->decay_coef) + 0.5);

    const __m256 v_scale_mpy   = _mm256_set1_ps(scale * (float)st->divs_for_dB);
    const __m256 v_corr        = _mm256_set1_ps((corr - (float)st->upper_pwr_bound) * (float)st->divs_for_dB);

    const __m256 sign_bit      = _mm256_set1_ps(-0.0f);
    const __m256i v_depth      = _mm256_set1_epi32((int32_t)rtsa_depth);
    const __m256 max_ind       = _mm256_set1_ps((float)(rtsa_depth - 1) - 0.5f);
    const __m256 ch_rate       = _mm256_set1_ps(charge_rate);
    const __m256 ch_norm_coef  = _mm256_set1_ps(CHARGE_NORM_COEF);
#ifdef USE_RTSA_ANTIALIASING
    const __m256 f_ones        = _mm256_set1_ps(1.0f);
#endif
    const __m256i v_maxcharge  = _mm256_set1_epi32(MAX_RTSA_PWR);
    const __m256i zeros        = _mm256_setzero_si256();

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
        // discharge all
        RTSA_U16_DISCHARGE(i, 16);

        __m256i s0 = _mm256_load_si256((__m256i*)&in[i]);
        __m256i ul0 = _mm256_unpacklo_epi16(s0, zeros);     // 11 10  9  8 - 3 2 1 0
        __m256i uh0 = _mm256_unpackhi_epi16(s0, zeros);     // 15 14 13 12 - 7 6 5 4

        __m256 l2_res0 = _mm256_cvtepi32_ps(ul0);
        __m256 l2_res1 = _mm256_cvtepi32_ps(uh0);

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

        // offsets
        //
        const __m256i idx0 = _mm256_set_epi32(i + 11, i + 10, i +  9, i +  8, i +  3, i +  2, i + 1, i + 0);
        const __m256i idx1 = _mm256_set_epi32(i + 15, i + 14, i + 13, i + 12, i +  7, i +  6, i + 5, i + 4);
        const __m256i fft_offs0 = _mm256_mullo_epi32(idx0, v_depth);
        const __m256i fft_offs1 = _mm256_mullo_epi32(idx1, v_depth);

#ifdef USE_RTSA_ANTIALIASING
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
        u_v8si_t
            lo0_offs = { _mm256_add_epi32(fft_offs0, _mm256_cvtps_epi32(lo0)) },
            lo1_offs = { _mm256_add_epi32(fft_offs1, _mm256_cvtps_epi32(lo1)) },
            hi0_offs = { _mm256_add_epi32(fft_offs0, _mm256_cvtps_epi32(hi0)) },
            hi1_offs = { _mm256_add_epi32(fft_offs1, _mm256_cvtps_epi32(hi1)) };

        u_v16si_t pwri_lo0 = {zeros};
        u_v16si_t pwri_lo1 = {zeros};
        u_v16si_t pwri_hi0 = {zeros};
        u_v16si_t pwri_hi1 = {zeros};

        for(unsigned j = 0, j2 = 0; j < 8; ++j, j2 += 2)
            pwri_lo0.arr[j2] = *(rtsa_data->pwr + lo0_offs.arr[j]);
        for(unsigned j = 0, j2 = 0; j < 8; ++j, j2 += 2)
            pwri_lo1.arr[j2] = *(rtsa_data->pwr + lo1_offs.arr[j]);
        for(unsigned j = 0, j2 = 0; j < 8; ++j, j2 += 2)
            pwri_hi0.arr[j2] = *(rtsa_data->pwr + hi0_offs.arr[j]);
        for(unsigned j = 0, j2 = 0; j < 8; ++j, j2 += 2)
            pwri_hi1.arr[j2] = *(rtsa_data->pwr + hi1_offs.arr[j]);

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
            *(rtsa_data->pwr + lo0_offs.arr[j]) = pwri_lo0.arr[j2];
        for( unsigned j = 0, j2 = 0; j < 8; ++j, j2 += 2)
            *(rtsa_data->pwr + lo1_offs.arr[j]) = pwri_lo1.arr[j2];
        for( unsigned j = 0, j2 = 0; j < 8; ++j, j2 += 2)
            *(rtsa_data->pwr + hi0_offs.arr[j]) = pwri_hi0.arr[j2];
        for( unsigned j = 0, j2 = 0; j < 8; ++j, j2 += 2)
            *(rtsa_data->pwr + hi1_offs.arr[j]) = pwri_hi1.arr[j2];
#else
        // load charge cells
        //
        u_v8si_t
            p0_offs = { _mm256_add_epi32(fft_offs0, _mm256_cvtps_epi32(pn0)) },
            p1_offs = { _mm256_add_epi32(fft_offs1, _mm256_cvtps_epi32(pn1)) };

        u_v16si_t pwri_p0 = {zeros};
        u_v16si_t pwri_p1 = {zeros};

        for(unsigned j = 0, j2 = 0; j < 8; ++j, j2 += 2)
            pwri_p0.arr[j2] = *(rtsa_data->pwr + p0_offs.arr[j]);

        __m256 pwr_p0 = _mm256_cvtepi32_ps(pwri_p0.vect);

        for(unsigned j = 0, j2 = 0; j < 8; ++j, j2 += 2)
            pwri_p1.arr[j2] = *(rtsa_data->pwr + p1_offs.arr[j]);

        __m256 pwr_p1 = _mm256_cvtepi32_ps(pwri_p1.vect);

        // charge
        //
        __m256i cdelta_p0 = _mm256_cvtps_epi32(_mm256_mul_ps(_mm256_sub_ps(ch_norm_coef, pwr_p0), ch_rate));
        __m256i cdelta_p1 = _mm256_cvtps_epi32(_mm256_mul_ps(_mm256_sub_ps(ch_norm_coef, pwr_p1), ch_rate));

        // store charged
        //
        pwri_p0.vect = _mm256_min_epu32(_mm256_add_epi32(pwri_p0.vect, cdelta_p0), v_maxcharge);

        for( unsigned j = 0, j2 = 0; j < 8; ++j, j2 += 2)
            *(rtsa_data->pwr + p0_offs.arr[j]) = pwri_p0.arr[j2];

        pwri_p1.vect = _mm256_min_epu32(_mm256_add_epi32(pwri_p1.vect, cdelta_p1), v_maxcharge);

        for( unsigned j = 0, j2 = 0; j < 8; ++j, j2 += 2)
            *(rtsa_data->pwr + p1_offs.arr[j]) = pwri_p1.arr[j2];
#endif
    }
}

#undef TEMPLATE_FUNC_NAME
