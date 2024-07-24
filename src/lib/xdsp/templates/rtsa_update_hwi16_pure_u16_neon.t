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

    const uint16_t nscale = (uint16_t)wvlt_log2f_fn(scale + 0.5);
    const uint16_t ndivs_for_dB = (uint16_t)wvlt_log2f_fn(st->divs_for_dB + 0.5);

    const unsigned decay_rate_pw2 =
        (unsigned)wvlt_log2f_fn(st->charging_frame) + (unsigned)wvlt_log2f_fn(st->decay_coef);

    const unsigned raise_rate_pw2 =
        (unsigned)wvlt_log2f_fn(st->charging_frame) - (unsigned)wvlt_log2f_fn(st->raise_coef) - ndivs_for_dB;

    const uint16_t nfft        = (uint16_t)wvlt_log2f_fn(fft_size);
    const uint16x8_t max_ind   = vdupq_n_u16(rtsa_depth - 1);
    const uint16x8_t v_maxcharge = vdupq_n_u16(MAX_RTSA_PWR);

    const unsigned discharge_add    = ((unsigned)(DISCHARGE_NORM_COEF) >> decay_rate_pw2);
    const uint16x8_t dch_add_coef   = vdupq_n_u16((uint16_t)discharge_add);

    const unsigned charge_add       = ((unsigned)(CHARGE_NORM_COEF) >> raise_rate_pw2);
    const uint16x8_t ch_add_coef    = vdupq_n_u16((uint16_t)charge_add);

    const uint16x8_t shr0 = vdupq_n_u16((uint8_t)(-nscale));
    const uint16x8_t shr1 = vdupq_n_u16((uint8_t)(-(HWI16_SCALE_N2_COEF - nscale > ndivs_for_dB ? HWI16_SCALE_N2_COEF - nscale - ndivs_for_dB : 16)));

    const uint16x8_t v_c1 = vdupq_n_u16(2 * HWI16_SCALE_COEF * nfft);
    const uint16x8_t v_c2 = vdupq_n_u16(((uint16_t)(- HWI16_CORR_COEF * 0.69897f) << ndivs_for_dB) + st->upper_pwr_bound);

    const unsigned rtsa_depth_bz = rtsa_depth * sizeof(rtsa_pwr_t);

    for (unsigned i = diap.from; i < diap.to; i += 16)
    {
        uint16x8_t s0 = vld1q_u16(&in[i + 0]);
        uint16x8_t s1 = vld1q_u16(&in[i + 8]);

        uint16x8_t p0 = vmulq_n_u16(vshlq_u16(vsubq_u16(s0, v_c1), shr0), (uint16_t)scale);
                   p0 = vabdq_u16(vshlq_u16(p0, shr1), v_c2);
                   p0 = vminq_u16(p0, max_ind);

        uint16x8_t p1 = vmulq_n_u16(vshlq_u16(vsubq_u16(s1, v_c1), shr0), (uint16_t)scale);
                   p1 = vabdq_u16(vshlq_u16(p1, shr1), v_c2);
                   p1 = vminq_u16(p1, max_ind);

        //load cells
        uint16x8_t pwr0, pwr1;

        for(unsigned j = 0; j < 8; ++j)
        {
            RTSA_GATHER_U16(pwr0, p0, 0, j)
            RTSA_GATHER_U16(pwr1, p1, 8, j)
        }

        uint16x8_t cdelta0, cdelta1;
        RTSA_SH_SWITCH(RTSA_CHARGE_SHIFT2, raise_rate_pw2);

        uint16x8_t cmdelta0 = vsubq_u16(v_maxcharge, pwr0);
        uint16x8_t cdelta_norm0 = vminq_u16(cdelta0, cmdelta0);
        pwr0 = vaddq_u16(pwr0, cdelta_norm0);

        uint16x8_t cmdelta1 = vsubq_u16(v_maxcharge, pwr1);
        uint16x8_t cdelta_norm1 = vminq_u16(cdelta1, cmdelta1);
        pwr1 = vaddq_u16(pwr1, cdelta_norm1);

        // store charged
        //
        for(unsigned j = 0; j < 8; ++j)
        {
            RTSA_SCATTER_U16(p0, pwr0, 0, j)
            RTSA_SCATTER_U16(p1, pwr1, 8, j)
        }

        // discharge all
        // note - we will discharge cells in the [i, i+8) fft band because those pages are already loaded to cache
        //
        RTSA_U16_DISCHARGE(16);
    }
}

#undef TEMPLATE_FUNC_NAME
