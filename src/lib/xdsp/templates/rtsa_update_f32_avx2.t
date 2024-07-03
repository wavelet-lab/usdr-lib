static
void TEMPLATE_FUNC_NAME(wvlt_fftwf_complex* __restrict in, unsigned fft_size,
                        fft_rtsa_data_t* __restrict rtsa_data,
                        float fcale_mpy, float mine, float corr)
{
    // Attention please!
    // rtsa_depth should be multiple to 8 here ( 8 = 32/sizeof(float) )
    // It will crash otherwise, due to aligning issues!
    //
    const fft_rtsa_settings_t * st = &rtsa_data->settings;
    const unsigned rtsa_depth = st->rtsa_depth;
    const float charge_rate = (float)st->raise_coef * st->divs_for_dB / st->charging_frame;
    const float discharge_rate = 1.0f / st->charging_frame / st->decay_coef;

    const __m256 v_scale_mpy   = _mm256_set1_ps(fcale_mpy);
    const __m256 v_mine        = _mm256_set1_ps(mine);
    const __m256 v_corr        = _mm256_set1_ps(corr - (float)st->upper_pwr_bound);
    const __m256 divs_for_dB   = _mm256_set1_ps((float)st->divs_for_dB);
#ifdef USE_POLYLOG2
    WVLT_POLYLOG2_DECL_CONSTS;
#else
    const __m256 log2_mul      = _mm256_set1_ps(WVLT_FASTLOG2_MUL);
    const __m256 log2_sub      = _mm256_set1_ps(WVLT_FASTLOG2_SUB);
#endif
    const __m256 sign_bit      = _mm256_set1_ps(-0.0f);
    const __m256i v_depth      = _mm256_set1_epi32((int32_t)rtsa_depth);
    const __m256 max_ind       = _mm256_set1_ps((float)(rtsa_depth - 1) - 0.5f);
    const __m256 ch_rate       = _mm256_set1_ps(charge_rate);
    const __m256 ch_norm_coef  = _mm256_set1_ps(CHARGE_NORM_COEF);
    const __m256 dch_norm_coef = _mm256_mul_ps(_mm256_set1_ps(discharge_rate), _mm256_set1_ps(DISCHARGE_NORM_COEF));
    const __m256 f_ones        = _mm256_set1_ps(1.0f);
    const __m256 f_maxcharge   = _mm256_set1_ps((float)MAX_RTSA_PWR);
    const __m256 f_zeros       = _mm256_setzero_ps();
    const __m256 dch_mul_a     = _mm256_sub_ps(f_ones, _mm256_set1_ps(discharge_rate));

    typedef int32_t v8si __attribute__ ((vector_size (32)));
    union u_v8si { __m256i vect; v8si arr; };
    typedef union u_v8si u_v8si_t;

    typedef float v8ps __attribute__ ((vector_size (32)));
    union u_v8ps { __m256 vect; v8ps arr; };
    typedef union u_v8ps u_v8ps_t;

    const unsigned rtsa_depth_bz = rtsa_depth * sizeof(float);

    for (unsigned i = 0; i < fft_size; i += 16)
    {

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
        __m256 p0raw = _mm256_andnot_ps(sign_bit, pwr0);
        __m256 p1raw = _mm256_andnot_ps(sign_bit, pwr1);

        // multiply to div cost
        //
        __m256 p0 = _mm256_mul_ps(p0raw, divs_for_dB);
        __m256 p1 = _mm256_mul_ps(p1raw, divs_for_dB);

        // normalize
        //
        __m256 pn0 = _mm256_min_ps(p0, max_ind);
        __m256 pn1 = _mm256_min_ps(p1, max_ind);

        // low bound
        //
        __m256 lo0 = _mm256_floor_ps(pn0);
        __m256 lo1 = _mm256_floor_ps(pn1);

        // high bound
        //
        __m256 hi0 = _mm256_ceil_ps(pn0);
        __m256 hi1 = _mm256_ceil_ps(pn1);

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

        __m256 pwr_lo0 = _mm256_i32gather_ps(rtsa_data->pwr, lo0_offs.vect, sizeof(float));
        __m256 pwr_lo1 = _mm256_i32gather_ps(rtsa_data->pwr, lo1_offs.vect, sizeof(float));
        __m256 pwr_hi0 = _mm256_i32gather_ps(rtsa_data->pwr, hi0_offs.vect, sizeof(float));
        __m256 pwr_hi1 = _mm256_i32gather_ps(rtsa_data->pwr, hi1_offs.vect, sizeof(float));

        // calc charge rates
        //
        __m256 charge_hi0 = _mm256_mul_ps( _mm256_sub_ps(pn0, lo0), ch_rate );
        __m256 charge_hi1 = _mm256_mul_ps( _mm256_sub_ps(pn1, lo1), ch_rate );
        __m256 charge_lo0 = _mm256_mul_ps( _mm256_sub_ps(hi0, pn0), ch_rate );
        __m256 charge_lo1 = _mm256_mul_ps( _mm256_sub_ps(hi1, pn1), ch_rate );

        // charge
        //
        __m256 ch_a_hi0 = _mm256_sub_ps(f_ones, charge_hi0);
        __m256 ch_a_hi1 = _mm256_sub_ps(f_ones, charge_hi1);
        __m256 ch_a_lo0 = _mm256_sub_ps(f_ones, charge_lo0);
        __m256 ch_a_lo1 = _mm256_sub_ps(f_ones, charge_lo1);

        __m256 ch_b_hi0 = _mm256_mul_ps(ch_norm_coef, charge_hi0);
        __m256 ch_b_hi1 = _mm256_mul_ps(ch_norm_coef, charge_hi1);
        __m256 ch_b_lo0 = _mm256_mul_ps(ch_norm_coef, charge_lo0);
        __m256 ch_b_lo1 = _mm256_mul_ps(ch_norm_coef, charge_lo1);

        __m256 new_pwr_hi0 = _mm256_min_ps( _mm256_add_ps( _mm256_mul_ps(pwr_hi0, ch_a_hi0), ch_b_hi0), f_maxcharge);
        __m256 new_pwr_hi1 = _mm256_min_ps( _mm256_add_ps( _mm256_mul_ps(pwr_hi1, ch_a_hi1), ch_b_hi1), f_maxcharge);
        __m256 new_pwr_lo0 = _mm256_min_ps( _mm256_add_ps( _mm256_mul_ps(pwr_lo0, ch_a_lo0), ch_b_lo0), f_maxcharge);
        __m256 new_pwr_lo1 = _mm256_min_ps( _mm256_add_ps( _mm256_mul_ps(pwr_lo1, ch_a_lo1), ch_b_lo1), f_maxcharge);

        // store charged
        //
        u_v8ps_t
            ch_res_hi0 = { new_pwr_hi0 },
            ch_res_hi1 = { new_pwr_hi1 },
            ch_res_lo0 = { new_pwr_lo0 },
            ch_res_lo1 = { new_pwr_lo1 };

        for( unsigned j = 0; j < 8; ++j)
        {
            rtsa_data->pwr[lo0_offs.arr[j]] = ch_res_lo0.arr[j];
            rtsa_data->pwr[lo1_offs.arr[j]] = ch_res_lo1.arr[j];
            rtsa_data->pwr[hi0_offs.arr[j]] = ch_res_hi0.arr[j];
            rtsa_data->pwr[hi1_offs.arr[j]] = ch_res_hi1.arr[j];
        }

        // discharge all
        // note - we will discharge cells in the [i, i+16) fft band because those pages are already loaded to cache
        //
        __m256 dpwr0, dpwr1, dpwr2, dpwr3;
        __m256 dp0, dp1, dp2, dp3;
        __m256 dpn0, dpn1, dpn2, dpn3;

        for(unsigned j = i; j < i + 16; ++j)
        {
            float* fptr = rtsa_data->pwr + j * rtsa_depth;
            unsigned n = rtsa_depth_bz;

            while(n >= 32 * sizeof(float))
            {
                dpwr0 = _mm256_load_ps(fptr +  0);
                dpwr1 = _mm256_load_ps(fptr +  8);
                dpwr2 = _mm256_load_ps(fptr + 16);
                dpwr3 = _mm256_load_ps(fptr + 24);

                dp0  = _mm256_sub_ps(_mm256_mul_ps(dpwr0, dch_mul_a), dch_norm_coef);
                dp1  = _mm256_sub_ps(_mm256_mul_ps(dpwr1, dch_mul_a), dch_norm_coef);
                dp2  = _mm256_sub_ps(_mm256_mul_ps(dpwr2, dch_mul_a), dch_norm_coef);
                dp3  = _mm256_sub_ps(_mm256_mul_ps(dpwr3, dch_mul_a), dch_norm_coef);
#if 1
                dpn0 = _mm256_max_ps(dp0, f_zeros);
                dpn1 = _mm256_max_ps(dp1, f_zeros);
                dpn2 = _mm256_max_ps(dp2, f_zeros);
                dpn3 = _mm256_max_ps(dp3, f_zeros);
#else
                dpn0 = _mm256_blendv_ps(dp0, f_zeros, dp0);
                dpn1 = _mm256_blendv_ps(dp1, f_zeros, dp1);
                dpn2 = _mm256_blendv_ps(dp2, f_zeros, dp2);
                dpn3 = _mm256_blendv_ps(dp3, f_zeros, dp3);
#endif
                _mm256_store_ps(fptr +  0, dpn0);
                _mm256_store_ps(fptr +  8, dpn1);
                _mm256_store_ps(fptr + 16, dpn2);
                _mm256_store_ps(fptr + 24, dpn3);

                n -= 32 * sizeof(float);
                fptr += 32;
            }

            while(n >= 16 * sizeof(float))
            {
                dpwr0 = _mm256_load_ps(fptr + 0);
                dpwr1 = _mm256_load_ps(fptr + 8);

                dp0  = _mm256_sub_ps(_mm256_mul_ps(dpwr0, dch_mul_a), dch_norm_coef);
                dp1  = _mm256_sub_ps(_mm256_mul_ps(dpwr1, dch_mul_a), dch_norm_coef);
#if 1
                dpn0 = _mm256_max_ps(dp0, f_zeros);
                dpn1 = _mm256_max_ps(dp1, f_zeros);
#else
                dpn0 = _mm256_blendv_ps(dp0, f_zeros, dp0);
                dpn1 = _mm256_blendv_ps(dp1, f_zeros, dp1);
#endif
                _mm256_store_ps(fptr + 0, dpn0);
                _mm256_store_ps(fptr + 8, dpn1);

                n -= 16 * sizeof(float);
                fptr += 16;
            }

            while(n >= 8 * sizeof(float))
            {
                dpwr0 = _mm256_load_ps(fptr + 0);

                dp0  = _mm256_sub_ps(_mm256_mul_ps(dpwr0, dch_mul_a), dch_norm_coef);
#if 1
                dpn0 = _mm256_max_ps(dp0, f_zeros);
#else
                dpn0 = _mm256_blendv_ps(dp0, f_zeros, dp0);
#endif
                _mm256_store_ps(fptr + 0, dpn0);

                n -= 8 * sizeof(float);
                fptr += 8;
            }
            // remember that rtsa_depth is multiple to 8, so we definitely have n == 0 here
        }
    }
}

#undef TEMPLATE_FUNC_NAME
