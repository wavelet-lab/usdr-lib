static
void TEMPLATE_FUNC_NAME(wvlt_fftwf_complex* __restrict in, unsigned fft_size,
                        fft_rtsa_data_t* __restrict rtsa_data,
                        float fcale_mpy, float mine, float corr)
{
    // Attention please!
    // rtsa_depth should be multiple to 16/sizeof(rtsa_pwr_t) here!
    // It will crash otherwise, due to aligning issues!
    //
    const fft_rtsa_settings_t * st = &rtsa_data->settings;
    const unsigned rtsa_depth = st->rtsa_depth;
    const float charge_rate = (float)st->raise_coef * st->divs_for_dB / st->charging_frame;
    const unsigned decay_rate_pw2 = (unsigned)(wvlt_fastlog2(st->charging_frame * st->decay_coef) + 0.5);
    const unsigned rtsa_depth_bz = rtsa_depth * sizeof(rtsa_pwr_t);

    const float32x4_t v_mine        = vdupq_n_f32(mine);
#ifdef USE_POLYLOG2
    WVLT_POLYLOG2_DECL_CONSTS;
#else
    const float32x4_t log2_sub      = vdupq_n_f32(-WVLT_FASTLOG2_SUB);
#endif
    const float32x4_t v_corr        = vdupq_n_f32(corr - (float)st->upper_pwr_bound);
    const float32x4_t max_ind       = vdupq_n_f32((float)(rtsa_depth - 1) - 0.5f);
    const float32x4_t f_ones        = vdupq_n_f32(1.0f);
    const float32x4_t f_maxcharge   = vdupq_n_f32((float)MAX_RTSA_PWR);

    const unsigned discharge_add    = ((unsigned)(DISCHARGE_NORM_COEF) >> decay_rate_pw2);
    const uint16x8_t dch_add_coef   = vdupq_n_u16((uint16_t)discharge_add);

#define RTSA_SHIFT4(n) \
    delta0 = vsraq_n_u16(dch_add_coef, d0, n); \
    delta1 = vsraq_n_u16(dch_add_coef, d1, n); \
    delta2 = vsraq_n_u16(dch_add_coef, d2, n); \
    delta3 = vsraq_n_u16(dch_add_coef, d3, n);

#define RTSA_SHIFT2(n) \
    delta0 = vsraq_n_u16(dch_add_coef, d0, n); \
    delta1 = vsraq_n_u16(dch_add_coef, d1, n);

#define RTSA_SHIFT1(n) \
    delta0 = vsraq_n_u16(dch_add_coef, d0, n);

#define RTSA_SH_SWITCH(shft) \
    switch(decay_rate_pw2) \
    { \
    case  1: shft(1); break; \
    case  2: shft(2); break; \
    case  3: shft(3); break; \
    case  4: shft(4); break; \
    case  5: shft(5); break; \
    case  6: shft(6); break; \
    case  7: shft(7); break; \
    case  8: shft(8); break; \
    case  9: shft(9); break; \
    case 10: shft(10); break; \
    case 11: shft(11); break; \
    case 12: shft(12); break; \
    case 13: shft(13); break; \
    case 14: shft(14); break; \
    case 15: shft(15); break; \
    default: shft(16); \
    }

#define RTSA_GATHER(dst, src, reg, n) \
    switch(n) \
    { \
    case 0 : dst = vld1_lane_u16(rtsa_data->pwr + (i + n + reg) * rtsa_depth + (unsigned)vgetq_lane_f32(src, 0), dst, 0); break; \
    case 1 : dst = vld1_lane_u16(rtsa_data->pwr + (i + n + reg) * rtsa_depth + (unsigned)vgetq_lane_f32(src, 1), dst, 1); break; \
    case 2 : dst = vld1_lane_u16(rtsa_data->pwr + (i + n + reg) * rtsa_depth + (unsigned)vgetq_lane_f32(src, 2), dst, 2); break; \
    case 3 : dst = vld1_lane_u16(rtsa_data->pwr + (i + n + reg) * rtsa_depth + (unsigned)vgetq_lane_f32(src, 3), dst, 3); break; \
    }

#define RTSA_SCATTER(dst, src, reg, n) \
    switch(n) \
    { \
    case 0 : vst1_lane_u16(rtsa_data->pwr + (i + n + reg) * rtsa_depth + (unsigned)vgetq_lane_f32(dst, 0), src, 0); break; \
    case 1 : vst1_lane_u16(rtsa_data->pwr + (i + n + reg) * rtsa_depth + (unsigned)vgetq_lane_f32(dst, 1), src, 1); break; \
    case 2 : vst1_lane_u16(rtsa_data->pwr + (i + n + reg) * rtsa_depth + (unsigned)vgetq_lane_f32(dst, 2), src, 2); break; \
    case 3 : vst1_lane_u16(rtsa_data->pwr + (i + n + reg) * rtsa_depth + (unsigned)vgetq_lane_f32(dst, 3), src, 3); break; \
    }

    for (unsigned i = 0; i < fft_size; i += 8)
    {
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
        float32x4_t pwr0 = vmlaq_n_f32(v_corr, l2_res0, fcale_mpy);
        float32x4_t pwr1 = vmlaq_n_f32(v_corr, l2_res1, fcale_mpy);

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
        uint16x4_t ipwr_lo0, ipwr_lo1, ipwr_hi0, ipwr_hi1;

        for(unsigned j = 0; j < 4; ++j)
        {
            RTSA_GATHER(ipwr_lo0, lo0, 0, j)
            RTSA_GATHER(ipwr_lo1, lo1, 4, j)
            RTSA_GATHER(ipwr_hi0, hi0, 0, j)
            RTSA_GATHER(ipwr_hi1, hi1, 4, j)
        }


        float32x4_t pwr_lo0 = vcvtq_f32_u32(vmovl_u16(ipwr_lo0));
        float32x4_t pwr_lo1 = vcvtq_f32_u32(vmovl_u16(ipwr_lo1));
        float32x4_t pwr_hi0 = vcvtq_f32_u32(vmovl_u16(ipwr_hi0));
        float32x4_t pwr_hi1 = vcvtq_f32_u32(vmovl_u16(ipwr_hi1));

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
        float32x4_t new_pwr_hi0 = vminq_f32( vmlaq_f32(ch_b_hi0, pwr_hi0, ch_a_hi0), f_maxcharge);
        float32x4_t new_pwr_hi1 = vminq_f32( vmlaq_f32(ch_b_hi1, pwr_hi1, ch_a_hi1), f_maxcharge);
        float32x4_t new_pwr_lo0 = vminq_f32( vmlaq_f32(ch_b_lo0, pwr_lo0, ch_a_lo0), f_maxcharge);
        float32x4_t new_pwr_lo1 = vminq_f32( vmlaq_f32(ch_b_lo1, pwr_lo1, ch_a_lo1), f_maxcharge);

        // store charged
        //
        ipwr_lo0 = vmovn_u32(vcvtq_u32_f32(new_pwr_lo0));
        ipwr_lo1 = vmovn_u32(vcvtq_u32_f32(new_pwr_lo1));
        ipwr_hi0 = vmovn_u32(vcvtq_u32_f32(new_pwr_hi0));
        ipwr_hi1 = vmovn_u32(vcvtq_u32_f32(new_pwr_hi1));

        for(unsigned j = 0; j < 4; ++j)
        {
            RTSA_SCATTER(lo0, ipwr_lo0, 0, j)
            RTSA_SCATTER(lo1, ipwr_lo1, 4, j)
            RTSA_SCATTER(hi0, ipwr_hi0, 0, j)
            RTSA_SCATTER(hi1, ipwr_hi1, 4, j)
        }

        // discharge all
        // note - we will discharge cells in the [i, i+8) fft band because those pages are already loaded to cache
        //

        uint16x8_t d0, d1, d2, d3;
        uint16x8_t delta0, delta1, delta2, delta3;
        uint16x8_t delta_norm0, delta_norm1, delta_norm2, delta_norm3;
        uint16x8_t res0, res1, res2, res3;

        for(unsigned j = i; j < i + 8; ++j)
        {
            uint16_t* ptr = rtsa_data->pwr + j * rtsa_depth;
            unsigned n = rtsa_depth_bz;

            while(n >= 64)
            {
                d0 = vld1q_u16(ptr + 0);
                d1 = vld1q_u16(ptr + 8);
                d2 = vld1q_u16(ptr + 16);
                d3 = vld1q_u16(ptr + 24);

                RTSA_SH_SWITCH(RTSA_SHIFT4)

                delta_norm0 = vminq_u16(delta0, d0);
                delta_norm1 = vminq_u16(delta1, d1);
                delta_norm2 = vminq_u16(delta2, d2);
                delta_norm3 = vminq_u16(delta3, d3);

                res0 = vsubq_u16(d0, delta_norm0);
                res1 = vsubq_u16(d1, delta_norm1);
                res2 = vsubq_u16(d2, delta_norm2);
                res3 = vsubq_u16(d3, delta_norm3);

                vst1q_u16(ptr + 0 , res0);
                vst1q_u16(ptr + 8 , res1);
                vst1q_u16(ptr + 16, res2);
                vst1q_u16(ptr + 24, res3);

                n -= 64;
                ptr += 32;
            }

            while(n >= 32)
            {
                d0 = vld1q_u16(ptr + 0);
                d1 = vld1q_u16(ptr + 8);

                RTSA_SH_SWITCH(RTSA_SHIFT2)

                delta_norm0 = vminq_u16(delta0, d0);
                delta_norm1 = vminq_u16(delta1, d1);

                res0 = vsubq_u16(d0, delta_norm0);
                res1 = vsubq_u16(d1, delta_norm1);

                vst1q_u16(ptr + 0 , res0);
                vst1q_u16(ptr + 8 , res1);

                n -= 32;
                ptr += 16;
            }

            while(n >= 16)
            {
                d0 = vld1q_u16(ptr + 0);
                RTSA_SH_SWITCH(RTSA_SHIFT1)
                delta_norm0 = vminq_u16(delta0, d0);
                res0 = vsubq_u16(d0, delta_norm0);
                vst1q_u16(ptr + 0 , res0);

                n -= 16;
                ptr += 8;
            }
            // we definitely have n == 0 here due to rtsa_depth aligning
        }
    }
}

#undef TEMPLATE_FUNC_NAME
