static
void TEMPLATE_FUNC_NAME(uint16_t* __restrict in, unsigned fft_size,
                        fft_rtsa_data_t* __restrict rtsa_data,
                        UNUSED float scale, UNUSED float corr, fft_diap_t diap, const rtsa_hwi16_consts_t* __restrict hwi16_consts)
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

    const unsigned decay_rate_pw2 =
        (unsigned)(wvlt_log2f_fn((float)st->charging_frame * st->decay_coef) + 0.5f);

    const unsigned raise_rate_pw2 =
        (unsigned)(wvlt_log2f_fn((float)st->charging_frame / st->raise_coef) + 0.5f) - hwi16_consts->ndivs_for_dB;

    const __m256i v_scale      = _mm256_set1_epi16((uint16_t)hwi16_consts->org_scale);
    const __m256i max_ind      = _mm256_set1_epi16(rtsa_depth - 1);

    const unsigned discharge_add = ((unsigned)(DISCHARGE_NORM_COEF) >> decay_rate_pw2);
    const __m256i dch_add_coef = _mm256_set1_epi16((uint16_t)discharge_add);
    const __m128i dch_rshift   = _mm_set_epi64x(0, decay_rate_pw2);

    const unsigned charge_add  = ((unsigned)(CHARGE_NORM_COEF) >> raise_rate_pw2);
    const __m256i ch_add_coef  = _mm256_set1_epi16((uint16_t)charge_add);
    const __m128i ch_rshift    = _mm_set_epi64x(0, raise_rate_pw2);

    const __m128i shr0 = _mm_set_epi64x(0, hwi16_consts->shr0);
    const __m128i shr1 = _mm_set_epi64x(0, hwi16_consts->shr1);

    const __m256i v_c0         = _mm256_set1_epi16(hwi16_consts->c0);
    const __m256i v_c1         = _mm256_set1_epi16(hwi16_consts->c1);

    typedef uint16_t v16si __attribute__ ((vector_size (32)));
    union u_v16si { __m256i vect; v16si arr; };
    typedef union u_v16si u_v16si_t;

    const __m256i v_depth_cfs  = _mm256_set_epi16(
                                       rtsa_depth * 15, rtsa_depth * 14, rtsa_depth * 13, rtsa_depth * 12,
                                       rtsa_depth * 11, rtsa_depth * 10, rtsa_depth *  9, rtsa_depth *  8,
                                       rtsa_depth *  7, rtsa_depth *  6, rtsa_depth *  5, rtsa_depth *  4,
                                       rtsa_depth *  3, rtsa_depth *  2, rtsa_depth *  1, rtsa_depth *  0);

    const unsigned rtsa_depth_bz = rtsa_depth * sizeof(rtsa_pwr_t);

    for (unsigned i = diap.from; i < diap.to; i += 64)
    {
        __m256i s0 = _mm256_load_si256((__m256i*)&in[i +  0]);
        __m256i s1 = _mm256_load_si256((__m256i*)&in[i + 16]);
        __m256i s2 = _mm256_load_si256((__m256i*)&in[i + 32]);
        __m256i s3 = _mm256_load_si256((__m256i*)&in[i + 48]);

        __m256i p0 = _mm256_mullo_epi16(_mm256_srl_epi16(_mm256_subs_epu16(s0, v_c0), shr0), v_scale);
                p0 = _mm256_abs_epi16(_mm256_sub_epi16(_mm256_srl_epi16(p0, shr1), v_c1));
                p0 = _mm256_min_epi16(p0, max_ind);

        __m256i p1 = _mm256_mullo_epi16(_mm256_srl_epi16(_mm256_subs_epu16(s1, v_c0), shr0), v_scale);
                p1 = _mm256_abs_epi16(_mm256_sub_epi16(_mm256_srl_epi16(p1, shr1), v_c1));
                p1 = _mm256_min_epi16(p1, max_ind);

        __m256i p2 = _mm256_mullo_epi16(_mm256_srl_epi16(_mm256_subs_epu16(s2, v_c0), shr0), v_scale);
                p2 = _mm256_abs_epi16(_mm256_sub_epi16(_mm256_srl_epi16(p2, shr1), v_c1));
                p2 = _mm256_min_epi16(p2, max_ind);

        __m256i p3 = _mm256_mullo_epi16(_mm256_srl_epi16(_mm256_subs_epu16(s3, v_c0), shr0), v_scale);
                p3 = _mm256_abs_epi16(_mm256_sub_epi16(_mm256_srl_epi16(p3, shr1), v_c1));
                p3 = _mm256_min_epi16(p3, max_ind);

        // discharge all
        RTSA_U16_DISCHARGE(i, 32);
        RTSA_U16_DISCHARGE(i+32, 32);

        // load charge cells
        //
        u_v16si_t pwr0, pwr1, pwr2, pwr3;

        u_v16si_t offs0 = {_mm256_adds_epu16(v_depth_cfs, p0)},
                  offs1 = {_mm256_adds_epu16(v_depth_cfs, p1)},
                  offs2 = {_mm256_adds_epu16(v_depth_cfs, p2)},
                  offs3 = {_mm256_adds_epu16(v_depth_cfs, p3)};

        uint16_t* ptr0 = rtsa_data->pwr + i * rtsa_depth;
        uint16_t* ptr1 = ptr0 + 16 * rtsa_depth;
        uint16_t* ptr2 = ptr1 + 16 * rtsa_depth;
        uint16_t* ptr3 = ptr2 + 16 * rtsa_depth;

        for(unsigned j = 0; j < 16; ++j)
            pwr0.arr[j] = *(ptr0 + offs0.arr[j]);
        for(unsigned j = 0; j < 16; ++j)
            pwr1.arr[j] = *(ptr1 + offs1.arr[j]);
        for(unsigned j = 0; j < 16; ++j)
            pwr2.arr[j] = *(ptr2 + offs2.arr[j]);
        for(unsigned j = 0; j < 16; ++j)
            pwr3.arr[j] = *(ptr3 + offs3.arr[j]);

        //Charge
        //
        __m256i cdelta0  = _mm256_subs_epu16(ch_add_coef, _mm256_srl_epi16(pwr0.vect, ch_rshift));
        __m256i cdelta1  = _mm256_subs_epu16(ch_add_coef, _mm256_srl_epi16(pwr1.vect, ch_rshift));
        __m256i cdelta2  = _mm256_subs_epu16(ch_add_coef, _mm256_srl_epi16(pwr2.vect, ch_rshift));
        __m256i cdelta3  = _mm256_subs_epu16(ch_add_coef, _mm256_srl_epi16(pwr3.vect, ch_rshift));

        pwr0.vect = _mm256_adds_epu16(pwr0.vect, cdelta0);
        pwr1.vect = _mm256_adds_epu16(pwr1.vect, cdelta1);
        pwr2.vect = _mm256_adds_epu16(pwr2.vect, cdelta2);
        pwr3.vect = _mm256_adds_epu16(pwr3.vect, cdelta3);

        //Store charged
        //
        for(unsigned j = 0; j < 16; ++j)
            *(ptr0 + offs0.arr[j]) = pwr0.arr[j];
        for(unsigned j = 0; j < 16; ++j)
            *(ptr1 + offs1.arr[j]) = pwr1.arr[j];
        for(unsigned j = 0; j < 16; ++j)
            *(ptr2 + offs2.arr[j]) = pwr2.arr[j];
        for(unsigned j = 0; j < 16; ++j)
            *(ptr3 + offs3.arr[j]) = pwr3.arr[j];
    }
}

#undef TEMPLATE_FUNC_NAME
