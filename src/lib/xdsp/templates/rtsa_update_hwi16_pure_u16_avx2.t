static
void TEMPLATE_FUNC_NAME(uint16_t* __restrict in, unsigned fft_size,
                        fft_rtsa_data_t* __restrict rtsa_data,
                        float scale, UNUSED float corr, fft_diap_t diap)
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

    for (unsigned i = diap.from; i < diap.to; i += 32)
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
        pwr0.vect = _mm256_adds_epu16(pwr0.vect, cdelta0);

        __m256i cdelta1  = _mm256_subs_epu16(ch_add_coef, _mm256_srl_epi16(pwr1.vect, ch_rshift));
        pwr1.vect = _mm256_adds_epu16(pwr1.vect, cdelta1);

        //Store charged
        //
        for(unsigned j = 0; j < 16; ++j)
        {
            rtsa_data->pwr[(i + j +  0) * rtsa_depth + p0.arr[j]] = pwr0.arr[j];
            rtsa_data->pwr[(i + j + 16) * rtsa_depth + p1.arr[j]] = pwr1.arr[j];
        }

        // discharge all
        // note - we will discharge cells in the [i, i+32) fft band because those pages are already loaded to cache
        //
        RTSA_U16_DISCHARGE(32);
    }
}

#undef TEMPLATE_FUNC_NAME
