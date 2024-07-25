static
void TEMPLATE_FUNC_NAME(uint16_t* __restrict in, unsigned fft_size,
                        fft_rtsa_data_t* __restrict rtsa_data,
                        float scale, float corr)
{
    // Attention please!
    // rtsa_depth should be multiple to 32/sizeof(rtsa_pwr_t) here!
    // It will crash otherwise, due to aligning issues!
    //
    const fft_rtsa_settings_t * st = &rtsa_data->settings;
    const unsigned rtsa_depth = st->rtsa_depth;
    const float charge_rate = (float)st->raise_coef * st->divs_for_dB / st->charging_frame;
    const unsigned decay_rate_pw2 = (unsigned)(wvlt_fastlog2(st->charging_frame * st->decay_coef) + 0.5);

    scale /= HWI16_SCALE_COEF;
    corr = corr / HWI16_SCALE_COEF + HWI16_CORR_COEF;

    const __m256 v_scale_mpy   = _mm256_set1_ps(scale);
    const __m256 v_corr        = _mm256_set1_ps(corr - (float)st->upper_pwr_bound);
    const __m256 divs_for_dB   = _mm256_set1_ps((float)st->divs_for_dB);

    const __m256 sign_bit      = _mm256_set1_ps(-0.0f);
    const __m256i v_depth      = _mm256_set1_epi32((int32_t)rtsa_depth);
    const __m256 max_ind       = _mm256_set1_ps((float)(rtsa_depth - 1) - 0.5f);
    const __m256 ch_rate       = _mm256_set1_ps(charge_rate);
    const __m256 ch_norm_coef  = _mm256_set1_ps(CHARGE_NORM_COEF);
    const __m256 f_ones        = _mm256_set1_ps(1.0f);
    const __m256 f_maxcharge   = _mm256_set1_ps((float)MAX_RTSA_PWR);

    const unsigned discharge_add = ((unsigned)(DISCHARGE_NORM_COEF) >> decay_rate_pw2);
    const __m256i dch_add_coef = _mm256_set1_epi16((uint16_t)discharge_add);
    const __m128i dch_rshift   = _mm_set_epi64x(0, decay_rate_pw2);

    typedef int32_t v8si __attribute__ ((vector_size (32)));
    union u_v8si { __m256i vect; v8si arr; };
    typedef union u_v8si u_v8si_t;

    typedef float v8ps __attribute__ ((vector_size (32)));
    union u_v8ps { __m256 vect; v8ps arr; };
    typedef union u_v8ps u_v8ps_t;

    const unsigned rtsa_depth_bz = rtsa_depth * sizeof(rtsa_pwr_t);

    for (unsigned i = 0; i < fft_size; i += 16)
    {
        __m256i s0 = _mm256_load_si256((__m256i*)&in[i]);

        __m256i dd0 = _mm256_cvtepu16_epi32(_mm256_castsi256_si128(s0));
        __m256i dd1 = _mm256_cvtepu16_epi32(_mm256_extracti128_si256(s0, 1));

        __m256 l2_res0 = _mm256_cvtepi32_ps(dd0);
        __m256 l2_res1 = _mm256_cvtepi32_ps(dd1);

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

        u_v8ps_t pwr_lo0, pwr_lo1, pwr_hi0, pwr_hi1;

        for(unsigned j = 0; j < 8; ++j)
        {
            pwr_lo0.arr[j] = (float)rtsa_data->pwr[lo0_offs.arr[j]];
            pwr_lo1.arr[j] = (float)rtsa_data->pwr[lo1_offs.arr[j]];
            pwr_hi0.arr[j] = (float)rtsa_data->pwr[hi0_offs.arr[j]];
            pwr_hi1.arr[j] = (float)rtsa_data->pwr[hi1_offs.arr[j]];
        }

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

        __m256 new_pwr_hi0 = _mm256_min_ps( _mm256_add_ps( _mm256_mul_ps(pwr_hi0.vect, ch_a_hi0), ch_b_hi0), f_maxcharge);
        __m256 new_pwr_hi1 = _mm256_min_ps( _mm256_add_ps( _mm256_mul_ps(pwr_hi1.vect, ch_a_hi1), ch_b_hi1), f_maxcharge);
        __m256 new_pwr_lo0 = _mm256_min_ps( _mm256_add_ps( _mm256_mul_ps(pwr_lo0.vect, ch_a_lo0), ch_b_lo0), f_maxcharge);
        __m256 new_pwr_lo1 = _mm256_min_ps( _mm256_add_ps( _mm256_mul_ps(pwr_lo1.vect, ch_a_lo1), ch_b_lo1), f_maxcharge);

        // store charged
        //
        u_v8ps_t
            ch_res_hi0 = { new_pwr_hi0 },
            ch_res_hi1 = { new_pwr_hi1 },
            ch_res_lo0 = { new_pwr_lo0 },
            ch_res_lo1 = { new_pwr_lo1 };

        for( unsigned j = 0; j < 8; ++j)
        {
            rtsa_data->pwr[lo0_offs.arr[j]] = (rtsa_pwr_t)ch_res_lo0.arr[j];
            rtsa_data->pwr[lo1_offs.arr[j]] = (rtsa_pwr_t)ch_res_lo1.arr[j];
            rtsa_data->pwr[hi0_offs.arr[j]] = (rtsa_pwr_t)ch_res_hi0.arr[j];
            rtsa_data->pwr[hi1_offs.arr[j]] = (rtsa_pwr_t)ch_res_hi1.arr[j];
        }

        // discharge all
        // note - we will discharge cells in the [i, i+16) fft band because those pages are already loaded to cache
        //

        __m256i d0, d1;
        __m256i da0, da1;
        __m256i delta0, delta1;
        __m256i delta_norm0, delta_norm1;
        __m256i res0, res1;

        for(unsigned j = i; j < i + 16; ++j)
        {
            __m256i* ptr = (__m256i*)(rtsa_data->pwr + j * rtsa_depth);
            unsigned n = rtsa_depth_bz;

            while(n >= 64)
            {

                d0 = _mm256_load_si256(ptr);
                d1 = _mm256_load_si256(ptr + 1);

                da0 = _mm256_srl_epi16(d0, dch_rshift);
                da1 = _mm256_srl_epi16(d1, dch_rshift);

                delta0 = _mm256_adds_epu16(da0, dch_add_coef);
                delta1 = _mm256_adds_epu16(da1, dch_add_coef);

                delta_norm0 = _mm256_min_epu16(delta0, d0);
                delta_norm1 = _mm256_min_epu16(delta1, d1);

                res0 = _mm256_subs_epu16(d0, delta_norm0);
                res1 = _mm256_subs_epu16(d1, delta_norm1);

                _mm256_store_si256(ptr++, res0);
                _mm256_store_si256(ptr++, res1);

                n -= 64;
            }

            while(n >= 32)
            {

                d0 = _mm256_load_si256(ptr);

                da0 = _mm256_srl_epi16(d0, dch_rshift);
                delta0 = _mm256_adds_epu16(da0, dch_add_coef);
                delta_norm0 = _mm256_min_epu16(delta0, d0);
                res0 = _mm256_subs_epu16(d0, delta_norm0);

                _mm256_store_si256(ptr++, res0);

                n -= 32;
            }
            // we definitely have n == 0 here due to rtsa_depth aligning
        }
    }
}

#undef TEMPLATE_FUNC_NAME
