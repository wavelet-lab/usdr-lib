static
void TEMPLATE_FUNC_NAME(uint16_t* __restrict in, unsigned fft_size,
                        fft_rtsa_data_t* __restrict rtsa_data,
                        float scale_mpy, float corr, fft_diap_t diap, UNUSED const rtsa_hwi16_consts_t* hwi16_consts)
{

#include "rtsa_update_u16_neon.inc"

#ifdef USE_POLYLOG2
    wvlt_log2f_fn_t wvlt_log2f_fn = wvlt_polylog2f;
#else
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
    const float scale = scale_mpy * (float)st->divs_for_dB;

    const float32x4_t v_corr        = vdupq_n_f32((corr - (float)st->upper_pwr_bound) * (float)st->divs_for_dB);
    const float32x4_t max_ind       = vdupq_n_f32((float)(rtsa_depth - 1) - 0.5f);
    const float32x4_t f_maxcharge   = vdupq_n_f32((float)MAX_RTSA_PWR);

    const unsigned discharge_add    = ((unsigned)(DISCHARGE_NORM_COEF) >> decay_rate_pw2);
    const uint16x8_t dch_add_coef   = vdupq_n_u16((uint16_t)discharge_add);

    for (unsigned i = diap.from; i < diap.to; i += 16)
    {
        const unsigned k = i - diap.from;

        uint16x8_t l2_0 = vld1q_u16(&in[k +  0]);
        uint16x8_t l2_1 = vld1q_u16(&in[k +  8]);

        float32x4_t l2_res0_0 = vcvtq_f32_u32( vmovl_u16(vget_low_u16(l2_0)) );
        float32x4_t l2_res1_0 = vcvtq_f32_u32( vmovl_u16(vget_high_u16(l2_0)) );
        float32x4_t l2_res0_1 = vcvtq_f32_u32( vmovl_u16(vget_low_u16(l2_1)) );
        float32x4_t l2_res1_1 = vcvtq_f32_u32( vmovl_u16(vget_high_u16(l2_1)) );

        // add scale & corr
        float32x4_t pw0_0 = vmlaq_n_f32(v_corr, l2_res0_0, scale);
        float32x4_t pw1_0 = vmlaq_n_f32(v_corr, l2_res1_0, scale);
        float32x4_t pw0_1 = vmlaq_n_f32(v_corr, l2_res0_1, scale);
        float32x4_t pw1_1 = vmlaq_n_f32(v_corr, l2_res1_1, scale);

        // drop sign
        //
        float32x4_t p0_0 = vabsq_f32(pw0_0);
        float32x4_t p1_0 = vabsq_f32(pw1_0);
        float32x4_t p0_1 = vabsq_f32(pw0_1);
        float32x4_t p1_1 = vabsq_f32(pw1_1);

        // normalize
        //
        float32x4_t pn0_0 = vminq_f32(p0_0, max_ind);
        float32x4_t pn1_0 = vminq_f32(p1_0, max_ind);
        float32x4_t pn0_1 = vminq_f32(p0_1, max_ind);
        float32x4_t pn1_1 = vminq_f32(p1_1, max_ind);

        // discharge all
        RTSA_U16_DISCHARGE(16);

        float32x4_t pwr0_0, pwr1_0, pwr0_1, pwr1_1;

        // load cells
        for(unsigned j = 0; j < 4; ++j)
        {
            pwr0_0[j] = (float)rtsa_data->pwr[(i + j +  0) * rtsa_depth + (unsigned)pn0_0[j]];
            pwr1_0[j] = (float)rtsa_data->pwr[(i + j +  4) * rtsa_depth + (unsigned)pn1_0[j]];
            pwr0_1[j] = (float)rtsa_data->pwr[(i + j +  8) * rtsa_depth + (unsigned)pn0_1[j]];
            pwr1_1[j] = (float)rtsa_data->pwr[(i + j + 12) * rtsa_depth + (unsigned)pn1_1[j]];
        }

        // charge
        //
        float32x4_t cdelta_p0_0 = vmulq_n_f32(vsubq_f32(ch_norm_coef, pwr0_0), charge_rate);
        float32x4_t cdelta_p1_0 = vmulq_n_f32(vsubq_f32(ch_norm_coef, pwr1_0), charge_rate);
        float32x4_t cdelta_p0_1 = vmulq_n_f32(vsubq_f32(ch_norm_coef, pwr0_1), charge_rate);
        float32x4_t cdelta_p1_1 = vmulq_n_f32(vsubq_f32(ch_norm_coef, pwr1_1), charge_rate);

        pwr0_0 = vminq_f32(vaddq_f32(pwr0_0, cdelta_p0_0), f_maxcharge);
        pwr1_0 = vminq_f32(vaddq_f32(pwr1_0, cdelta_p1_0), f_maxcharge);
        pwr0_1 = vminq_f32(vaddq_f32(pwr0_1, cdelta_p0_1), f_maxcharge);
        pwr1_1 = vminq_f32(vaddq_f32(pwr1_1, cdelta_p1_1), f_maxcharge);

        // store charged
        //
        for(unsigned j = 0; j < 4; ++j)
        {
            rtsa_data->pwr[(i + j +  0) * rtsa_depth + (unsigned)pn0_0[j]] = (uint16_t)pwr0_0[j];
            rtsa_data->pwr[(i + j +  4) * rtsa_depth + (unsigned)pn1_0[j]] = (uint16_t)pwr1_0[j];
            rtsa_data->pwr[(i + j +  8) * rtsa_depth + (unsigned)pn0_1[j]] = (uint16_t)pwr0_1[j];
            rtsa_data->pwr[(i + j + 12) * rtsa_depth + (unsigned)pn1_1[j]] = (uint16_t)pwr1_1[j];
        }
    }
}

#undef TEMPLATE_FUNC_NAME
