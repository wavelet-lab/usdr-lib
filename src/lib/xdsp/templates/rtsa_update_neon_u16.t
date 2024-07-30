static
void TEMPLATE_FUNC_NAME(wvlt_fftwf_complex* __restrict in, unsigned fft_size,
                        fft_rtsa_data_t* __restrict rtsa_data,
                        float fcale_mpy, float mine, float corr, fft_diap_t diap)
{

#include "rtsa_update_u16_neon.inc"

#ifdef USE_POLYLOG2
    WVLT_POLYLOG2_DECL_CONSTS;
    wvlt_log2f_fn_t wvlt_log2f_fn = wvlt_polylog2f;
#else
    const float32x4_t log2_sub    = vdupq_n_f32(-WVLT_FASTLOG2_SUB);
    wvlt_log2f_fn_t wvlt_log2f_fn = wvlt_fastlog2;
#endif

    // Attention please!
    // rtsa_depth should be multiple to 16/sizeof(rtsa_pwr_t) here!
    // It will crash otherwise, due to aligning issues!
    //
    const fft_rtsa_settings_t * st = &rtsa_data->settings;
    const unsigned rtsa_depth = st->rtsa_depth;
    const float charge_rate = (float)st->raise_coef * st->divs_for_dB / st->charging_frame;
    const float32x4_t ch_norm_coef  = vdupq_n_f32(CHARGE_NORM_COEF);

    const unsigned decay_rate_pw2 = (unsigned)(wvlt_log2f_fn(st->charging_frame * st->decay_coef) + 0.5);
    const int16x8_t decay_shr = vdupq_n_s16((uint8_t)(-decay_rate_pw2));
    const unsigned rtsa_depth_bz = rtsa_depth * sizeof(rtsa_pwr_t);
    const float scale = fcale_mpy * (float)st->divs_for_dB;

    const float32x4_t v_mine        = vdupq_n_f32(mine);
    const float32x4_t v_corr        = vdupq_n_f32((corr - (float)st->upper_pwr_bound) * (float)st->divs_for_dB);
    const float32x4_t max_ind       = vdupq_n_f32((float)(rtsa_depth - 1) - 0.5f);
#ifdef USE_RTSA_ANTIALIASING
    const float32x4_t f_ones        = vdupq_n_f32(1.0f);
#endif
    const float32x4_t f_maxcharge   = vdupq_n_f32((float)MAX_RTSA_PWR);

    const unsigned discharge_add    = ((unsigned)(DISCHARGE_NORM_COEF) >> decay_rate_pw2);
    const uint16x8_t dch_add_coef   = vdupq_n_u16((uint16_t)discharge_add);

    for (unsigned i = diap.from; i < diap.to; i += 8)
    {
        // discharge all
        RTSA_U16_DISCHARGE(8);

        // load 8 complex pairs = 16 floats = 64b = 512bits
        //
        float32x4x2_t e0 = vld2q_f32(&in[i + 0][0]);
        float32x4x2_t e1 = vld2q_f32(&in[i + 4][0]);

        float32x4_t summ0 = vmlaq_f32(vmlaq_f32(v_mine, e0.val[0], e0.val[0]), e0.val[1], e0.val[1]);
        float32x4_t summ1 = vmlaq_f32(vmlaq_f32(v_mine, e1.val[0], e1.val[0]), e1.val[1], e1.val[1]);

#ifdef USE_POLYLOG2
        float32x4_t l2_res0, l2_res1;
        WVLT_POLYLOG2F8(summ0, l2_res0);
        WVLT_POLYLOG2F8(summ1, l2_res1);
#else
        // fasterlog2
        //
        float32x4_t summ0_ = vcvtq_f32_u32(vreinterpretq_u32_f32(summ0));
        float32x4_t summ1_ = vcvtq_f32_u32(vreinterpretq_u32_f32(summ1));
        float32x4_t l2_res0 = vmlaq_n_f32(log2_sub, summ0_, WVLT_FASTLOG2_MUL);
        float32x4_t l2_res1 = vmlaq_n_f32(log2_sub, summ1_, WVLT_FASTLOG2_MUL);
#endif
        // add scale & corr
        float32x4_t pw0 = vmlaq_n_f32(v_corr, l2_res0, scale);
        float32x4_t pw1 = vmlaq_n_f32(v_corr, l2_res1, scale);

        // drop sign
        //
        float32x4_t p0 = vabsq_f32(pw0);
        float32x4_t p1 = vabsq_f32(pw1);

        // normalize
        //
        float32x4_t pn0 = vminq_f32(p0, max_ind);
        float32x4_t pn1 = vminq_f32(p1, max_ind);

#ifdef USE_RTSA_ANTIALIASING
        // low bound
        //
        float32x4_t lo0 = vrndq_f32(pn0);
        float32x4_t lo1 = vrndq_f32(pn1);

        // high bound
        //
        float32x4_t hi0 = vaddq_f32(lo0, f_ones);
        float32x4_t hi1 = vaddq_f32(lo1, f_ones);

        //load cells
        float32x4_t pwr_lo0, pwr_lo1, pwr_hi0, pwr_hi1;

        for(unsigned j = 0; j < 4; ++j)
        {
            pwr_lo0[j] = (float)rtsa_data->pwr[(i + j + 0) * rtsa_depth + (unsigned)lo0[j]];
            pwr_lo1[j] = (float)rtsa_data->pwr[(i + j + 4) * rtsa_depth + (unsigned)lo1[j]];
            pwr_hi0[j] = (float)rtsa_data->pwr[(i + j + 0) * rtsa_depth + (unsigned)hi0[j]];
            pwr_hi1[j] = (float)rtsa_data->pwr[(i + j + 4) * rtsa_depth + (unsigned)hi1[j]];
        }

        // calc charge rates
        //
        float32x4_t charge_hi0 = vmulq_n_f32(vsubq_f32(pn0, lo0), charge_rate);
        float32x4_t charge_hi1 = vmulq_n_f32(vsubq_f32(pn1, lo1), charge_rate);
        float32x4_t charge_lo0 = vmulq_n_f32(vsubq_f32(hi0, pn0), charge_rate);
        float32x4_t charge_lo1 = vmulq_n_f32(vsubq_f32(hi1, pn1), charge_rate);

        // charge
        //
        float32x4_t cdelta_lo0 = vmulq_f32(vsubq_f32(ch_norm_coef, pwr_lo0), charge_lo0);
        float32x4_t cdelta_hi0 = vmulq_f32(vsubq_f32(ch_norm_coef, pwr_hi0), charge_hi0);
        float32x4_t cdelta_lo1 = vmulq_f32(vsubq_f32(ch_norm_coef, pwr_lo1), charge_lo1);
        float32x4_t cdelta_hi1 = vmulq_f32(vsubq_f32(ch_norm_coef, pwr_hi1), charge_hi1);

        pwr_lo0 = vminq_f32(vaddq_f32(pwr_lo0, cdelta_lo0), f_maxcharge);
        pwr_hi0 = vminq_f32(vaddq_f32(pwr_hi0, cdelta_hi0), f_maxcharge);
        pwr_lo1 = vminq_f32(vaddq_f32(pwr_lo1, cdelta_lo1), f_maxcharge);
        pwr_hi1 = vminq_f32(vaddq_f32(pwr_hi1, cdelta_hi1), f_maxcharge);

        // store charged
        //
        for(unsigned j = 0; j < 4; ++j)
        {
            rtsa_data->pwr[(i + j + 0) * rtsa_depth + (unsigned)lo0[j]] = (uint16_t)pwr_lo0[j];
            rtsa_data->pwr[(i + j + 4) * rtsa_depth + (unsigned)lo1[j]] = (uint16_t)pwr_lo1[j];
            rtsa_data->pwr[(i + j + 0) * rtsa_depth + (unsigned)hi0[j]] = (uint16_t)pwr_hi0[j];
            rtsa_data->pwr[(i + j + 4) * rtsa_depth + (unsigned)hi1[j]] = (uint16_t)pwr_hi1[j];
        }
#else
        float32x4_t pwr0, pwr1;

        // load cells
        for(unsigned j = 0; j < 4; ++j)
        {
            pwr0[j] = (float)rtsa_data->pwr[(i + j + 0) * rtsa_depth + (unsigned)pn0[j]];
            pwr1[j] = (float)rtsa_data->pwr[(i + j + 4) * rtsa_depth + (unsigned)pn1[j]];
        }

        // charge
        //
        float32x4_t cdelta_p0 = vmulq_n_f32(vsubq_f32(ch_norm_coef, pwr0), charge_rate);
        float32x4_t cdelta_p1 = vmulq_n_f32(vsubq_f32(ch_norm_coef, pwr1), charge_rate);

        pwr0 = vminq_f32(vaddq_f32(pwr0, cdelta_p0), f_maxcharge);
        pwr1 = vminq_f32(vaddq_f32(pwr1, cdelta_p1), f_maxcharge);

        // store charged
        //
        for(unsigned j = 0; j < 4; ++j)
        {
            rtsa_data->pwr[(i + j + 0) * rtsa_depth + (unsigned)pn0[j]] = (uint16_t)pwr0[j];
            rtsa_data->pwr[(i + j + 4) * rtsa_depth + (unsigned)pn1[j]] = (uint16_t)pwr1[j];
        }
#endif
    }
}

#undef TEMPLATE_FUNC_NAME
