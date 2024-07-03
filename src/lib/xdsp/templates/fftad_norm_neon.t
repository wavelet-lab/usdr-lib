static
void TEMPLATE_FUNC_NAME(fft_acc_t* __restrict p, unsigned fftsz, float scale, float corr, float* __restrict outa)
{
    const unsigned half = fftsz >> 1;
#ifdef USE_POLYLOG2
    WVLT_POLYLOG2_DECL_CONSTS;
#else
    const float32x4_t log2_sub = vdupq_n_f32(-WVLT_FASTLOG2_SUB);
#endif
    const float32x4_t vcorr    = vdupq_n_f32(corr);

    for(unsigned i = 0; i < fftsz; i += 8)
    {
        const float32x4_t m0 = vld1q_f32(p->f_mant + i + 0);
        const float32x4_t m1 = vld1q_f32(p->f_mant + i + 4);

        const int32x4_t   p0 = vld1q_s32(p->f_pwr + i + 0);
        const int32x4_t   p1 = vld1q_s32(p->f_pwr + i + 4);

#ifdef USE_POLYLOG2
        float32x4_t apwr0, apwr1;
        WVLT_POLYLOG2F8(m0, apwr0);
        WVLT_POLYLOG2F8(m1, apwr1);
#else
        // fasterlog2
        //
        float32x4_t l20 = vcvtq_f32_u32(vreinterpretq_u32_f32(m0));
        float32x4_t l21 = vcvtq_f32_u32(vreinterpretq_u32_f32(m1));
        float32x4_t apwr0 = vmlaq_n_f32(log2_sub, l20, WVLT_FASTLOG2_MUL);
        float32x4_t apwr1 = vmlaq_n_f32(log2_sub, l21, WVLT_FASTLOG2_MUL);
#endif
        float32x4_t s0 = vaddq_f32(apwr0, vcvtq_f32_s32(p0));
        float32x4_t s1 = vaddq_f32(apwr1, vcvtq_f32_s32(p1));

        float32x4_t f0 = vmlaq_n_f32(vcorr, s0, scale);
        float32x4_t f1 = vmlaq_n_f32(vcorr, s1, scale);

        int32_t offset;

        if(i + 8 <= half)
        {
            offset = half;
        }
        else if(i >= half)
        {
            offset = - half;
        }
        else
        {
            offset = 0;
        }

        vst1q_f32(outa + i + offset + 0, f0);
        vst1q_f32(outa + i + offset + 4, f1);
    }
}

#undef TEMPLATE_FUNC_NAME
