static
void TEMPLATE_FUNC_NAME(uint16_t* __restrict in, unsigned fft_size,
                        fft_rtsa_data_t* __restrict rtsa_data,
                        float scale, UNUSED float corr)
{
    // Attention please!
    // rtsa_depth should be multiple to 32/sizeof(rtsa_pwr_t) here!
    // It will crash otherwise, due to aligning issues!
    //

#ifdef USE_POLYLOG2
    wvlt_log2f_fn_t wvlt_log2f_fn = wvlt_polylog2f;
#else
    wvlt_log2f_fn_t wvlt_log2f_fn = wvlt_fastlog2;
#endif

    const fft_rtsa_settings_t * st = &rtsa_data->settings;
    const uint16_t rtsa_depth = st->rtsa_depth;

    const uint16_t nscale = (uint16_t)wvlt_log2f_fn(scale + 0.5);
    const uint16_t ndivs_for_dB = (uint16_t)wvlt_log2f_fn(st->divs_for_dB + 0.5);

    const unsigned decay_rate_pw2 =
        (unsigned)wvlt_log2f_fn(st->charging_frame) + (unsigned)wvlt_log2f_fn(st->decay_coef);

    const unsigned raise_rate_pw2 =
        (unsigned)wvlt_log2f_fn(st->charging_frame) - (unsigned)wvlt_log2f_fn(st->raise_coef) - ndivs_for_dB;

    const uint16_t nfft        = (uint16_t)wvlt_log2f_fn(fft_size);
    const __m256i v_scale      = _mm256_set1_epi16((uint16_t)scale);

    const __m256i max_ind      = _mm256_set1_epi16(rtsa_depth - 1);
    const __m256i v_maxcharge  = _mm256_set1_epi16(MAX_RTSA_PWR);

    const unsigned discharge_add = ((unsigned)(DISCHARGE_NORM_COEF) >> decay_rate_pw2);
    const __m256i dch_add_coef = _mm256_set1_epi16((uint16_t)discharge_add);
    const __m128i dch_rshift   = _mm_set_epi64x(0, decay_rate_pw2);

    const unsigned charge_add  = ((unsigned)(CHARGE_NORM_COEF) >> raise_rate_pw2);
    const __m256i ch_add_coef  = _mm256_set1_epi16((uint16_t)charge_add);
    const __m128i ch_rshift    = _mm_set_epi64x(0, raise_rate_pw2);

    const __m128i shr0 = _mm_set_epi64x(0, nscale);
    const __m128i shr1 = _mm_set_epi64x(0, HWI16_SCALE_N2_COEF - nscale > ndivs_for_dB ? HWI16_SCALE_N2_COEF - nscale - ndivs_for_dB : 16);

    const __m256i v_c1         = _mm256_set1_epi16(2 * HWI16_SCALE_COEF * nfft);
    const __m256i v_c2         = _mm256_set1_epi16(((uint16_t)(- HWI16_CORR_COEF * 0.69897f) << ndivs_for_dB) + st->upper_pwr_bound);

    typedef uint16_t v16si __attribute__ ((vector_size (32)));
    union u_v16si { __m256i vect; v16si arr; };
    typedef union u_v16si u_v16si_t;

    const unsigned rtsa_depth_bz = rtsa_depth * sizeof(rtsa_pwr_t);

    for (unsigned i = 0; i < fft_size; i += 32)
    {
        __m256i s0 = _mm256_load_si256((__m256i*)&in[i +  0]);
        __m256i s1 = _mm256_load_si256((__m256i*)&in[i + 16]);

        u_v16si_t p0      = {_mm256_mullo_epi16(_mm256_srl_epi16(_mm256_subs_epu16(s0, v_c1), shr0), v_scale)};
                  p0.vect = _mm256_abs_epi16(_mm256_sub_epi16(_mm256_srl_epi16(p0.vect, shr1), v_c2));
                  p0.vect = _mm256_min_epi16(p0.vect, max_ind);

        u_v16si_t p1      = {_mm256_mullo_epi16(_mm256_srl_epi16(_mm256_subs_epu16(s1, v_c1), shr0), v_scale)};
                  p1.vect = _mm256_abs_epi16(_mm256_sub_epi16(_mm256_srl_epi16(p1.vect, shr1), v_c2));
                  p1.vect = _mm256_min_epi16(p1.vect, max_ind);

        // load charge cells
        //
        u_v16si_t pwr0, pwr1;

        for(unsigned j = 0; j < 16; ++j)
        {
            pwr0.arr[j] = rtsa_data->pwr[(i + j +  0) * rtsa_depth + p0.arr[j]];
            pwr1.arr[j] = rtsa_data->pwr[(i + j + 16) * rtsa_depth + p1.arr[j]];
        }

        //Charge
        //
        __m256i cdelta0  = _mm256_subs_epu16(ch_add_coef, _mm256_srl_epi16(pwr0.vect, ch_rshift));
        __m256i cmdelta0 = _mm256_subs_epu16(v_maxcharge, pwr0.vect);
        __m256i cdelta_norm0 = _mm256_min_epu16(cdelta0, cmdelta0);
        pwr0.vect = _mm256_adds_epu16(pwr0.vect, cdelta_norm0);

        __m256i cdelta1  = _mm256_subs_epu16(ch_add_coef, _mm256_srl_epi16(pwr1.vect, ch_rshift));
        __m256i cmdelta1 = _mm256_subs_epu16(v_maxcharge, pwr1.vect);
        __m256i cdelta_norm1 = _mm256_min_epu16(cdelta1, cmdelta1);
        pwr1.vect = _mm256_adds_epu16(pwr1.vect, cdelta_norm1);

        //Store charged
        //
        for(unsigned j = 0; j < 16; ++j)
        {
            rtsa_data->pwr[(i + j +  0) * rtsa_depth + p0.arr[j]] = pwr0.arr[j];
            rtsa_data->pwr[(i + j + 16) * rtsa_depth + p1.arr[j]] = pwr1.arr[j];
        }

        // discharge all
        // note - we will discharge cells in the [i, i+16) fft band because those pages are already loaded to cache
        //

        __m256i d0, d1, d2, d3;
        __m256i da0, da1, da2, da3;
        __m256i delta0, delta1, delta2, delta3;
        __m256i delta_norm0, delta_norm1, delta_norm2, delta_norm3;
        __m256i res0, res1, res2, res3;

        for(unsigned j = i; j < i + 32; ++j)
        {
            __m256i* ptr = (__m256i*)(rtsa_data->pwr + j * rtsa_depth);
            unsigned n = rtsa_depth_bz;

            while(n >= 128)
            {
                d0 = _mm256_load_si256(ptr + 0);
                d1 = _mm256_load_si256(ptr + 1);

                da0 = _mm256_srl_epi16(d0, dch_rshift);
                da1 = _mm256_srl_epi16(d1, dch_rshift);

                delta0 = _mm256_adds_epu16(da0, dch_add_coef);
                delta1 = _mm256_adds_epu16(da1, dch_add_coef);

                delta_norm0 = _mm256_min_epu16(delta0, d0);
                delta_norm1 = _mm256_min_epu16(delta1, d1);

                res0 = _mm256_subs_epu16(d0, delta_norm0);
                res1 = _mm256_subs_epu16(d1, delta_norm1);

                d2 = _mm256_load_si256(ptr + 2);
                d3 = _mm256_load_si256(ptr + 3);

                da2 = _mm256_srl_epi16(d2, dch_rshift);
                da3 = _mm256_srl_epi16(d3, dch_rshift);

                delta2 = _mm256_adds_epu16(da2, dch_add_coef);
                delta3 = _mm256_adds_epu16(da3, dch_add_coef);

                delta_norm2 = _mm256_min_epu16(delta2, d2);
                delta_norm3 = _mm256_min_epu16(delta3, d3);

                res2 = _mm256_subs_epu16(d2, delta_norm2);
                res3 = _mm256_subs_epu16(d3, delta_norm3);

                _mm256_store_si256(ptr++, res0);
                _mm256_store_si256(ptr++, res1);
                _mm256_store_si256(ptr++, res2);
                _mm256_store_si256(ptr++, res3);

                n -= 128;
            }

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
