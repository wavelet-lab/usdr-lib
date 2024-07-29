static
void TEMPLATE_FUNC_NAME(uint16_t* __restrict in, unsigned fft_size,
                        fft_rtsa_data_t* __restrict rtsa_data,
                        float scale, float corr, fft_diap_t diap)
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
    const unsigned decay_rate_pw2 = (unsigned)(wvlt_log2f_fn(st->charging_frame * st->decay_coef) + 0.5);
    const int16x8_t decay_shr = vdupq_n_s16((uint8_t)(-decay_rate_pw2));
    const unsigned rtsa_depth_bz = rtsa_depth * sizeof(rtsa_pwr_t);

    const float32x4_t v_corr        = vdupq_n_f32(corr - (float)st->upper_pwr_bound);
    const float32x4_t max_ind       = vdupq_n_f32((float)(rtsa_depth - 1) - 0.5f);
    const float32x4_t f_ones        = vdupq_n_f32(1.0f);
    const float32x4_t f_maxcharge   = vdupq_n_f32((float)MAX_RTSA_PWR);

    const unsigned discharge_add    = ((unsigned)(DISCHARGE_NORM_COEF) >> decay_rate_pw2);
    const uint16x8_t dch_add_coef   = vdupq_n_u16((uint16_t)discharge_add);

    for (unsigned i = diap.from; i < diap.to; i += 8)
    {
        uint16x8_t l2 = vld1q_u16(&in[i]);

        float32x4_t l2_res0 = vcvtq_f32_u32( vmovl_u16(vget_low_u16(l2)) );
        float32x4_t l2_res1 = vcvtq_f32_u32( vmovl_u16(vget_high_u16(l2)) );

        // add scale & corr
        float32x4_t pwr0 = vmlaq_n_f32(v_corr, l2_res0, scale);
        float32x4_t pwr1 = vmlaq_n_f32(v_corr, l2_res1, scale);

        // drop sign
        //
        float32x4_t p0raw = vabsq_f32(pwr0);
        float32x4_t p1raw = vabsq_f32(pwr1);

        // multiply to div cost
        //
        float32x4_t p0 = vmulq_n_f32(p0raw, (float)st->divs_for_dB);
        float32x4_t p1 = vmulq_n_f32(p1raw, (float)st->divs_for_dB);

        // normalize
        //
        float32x4_t pn0 = vminq_f32(p0, max_ind);
        float32x4_t pn1 = vminq_f32(p1, max_ind);

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

        float32x4_t ch_a_hi0 = vsubq_f32(f_ones, charge_hi0);
        float32x4_t ch_a_hi1 = vsubq_f32(f_ones, charge_hi1);
        float32x4_t ch_a_lo0 = vsubq_f32(f_ones, charge_lo0);
        float32x4_t ch_a_lo1 = vsubq_f32(f_ones, charge_lo1);

        float32x4_t ch_b_hi0 = vmulq_n_f32(charge_hi0, CHARGE_NORM_COEF);
        float32x4_t ch_b_hi1 = vmulq_n_f32(charge_hi1, CHARGE_NORM_COEF);
        float32x4_t ch_b_lo0 = vmulq_n_f32(charge_lo0, CHARGE_NORM_COEF);
        float32x4_t ch_b_lo1 = vmulq_n_f32(charge_lo1, CHARGE_NORM_COEF);

        // charge
        //
        pwr_hi0 = vminq_f32( vmlaq_f32(ch_b_hi0, pwr_hi0, ch_a_hi0), f_maxcharge);
        pwr_hi1 = vminq_f32( vmlaq_f32(ch_b_hi1, pwr_hi1, ch_a_hi1), f_maxcharge);
        pwr_lo0 = vminq_f32( vmlaq_f32(ch_b_lo0, pwr_lo0, ch_a_lo0), f_maxcharge);
        pwr_lo1 = vminq_f32( vmlaq_f32(ch_b_lo1, pwr_lo1, ch_a_lo1), f_maxcharge);

        // store charged
        //
        for(unsigned j = 0; j < 4; ++j)
        {
            rtsa_data->pwr[(i + j + 0) * rtsa_depth + (unsigned)lo0[j]] = (uint16_t)pwr_lo0[j];
            rtsa_data->pwr[(i + j + 4) * rtsa_depth + (unsigned)lo1[j]] = (uint16_t)pwr_lo1[j];
            rtsa_data->pwr[(i + j + 0) * rtsa_depth + (unsigned)hi0[j]] = (uint16_t)pwr_hi0[j];
            rtsa_data->pwr[(i + j + 4) * rtsa_depth + (unsigned)hi1[j]] = (uint16_t)pwr_hi1[j];
        }

        // discharge all
        // note - we will discharge cells in the [i, i+8) fft band because those pages are already loaded to cache
        //
        RTSA_U16_DISCHARGE(8);
    }
}

#undef TEMPLATE_FUNC_NAME
