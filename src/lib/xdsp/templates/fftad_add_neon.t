static
void TEMPLATE_FUNC_NAME(fft_acc_t* __restrict p, wvlt_fftwf_complex* __restrict d, unsigned fftsz)
{
    const uint32x4_t  fnoexp  = vdupq_n_u32(~(0xffu << 23));
    const uint32x4_t  fexp0   = vdupq_n_u32(127u << 23);
    const int32x4_t   expcorr = vdupq_n_s32(127);
    const float32x4_t mine    = vdupq_n_f32(p->mine);

    for (unsigned i = 0; i < fftsz; i += 8)
    {
        // load 8 complex pairs = 16 floats = 64b = 512bits
        //
        float32x4x2_t e0 = vld2q_f32(&d[i + 0][0]);
        float32x4x2_t e1 = vld2q_f32(&d[i + 4][0]);

        float32x4_t enz0 = vmlaq_f32(vmlaq_f32(mine, e0.val[0], e0.val[0]), e0.val[1], e0.val[1]);
        float32x4_t enz1 = vmlaq_f32(vmlaq_f32(mine, e1.val[0], e1.val[0]), e1.val[1], e1.val[1]);

        float32x4_t acc_m0 = vld1q_f32(&p->f_mant[i + 0]);
        float32x4_t acc_m1 = vld1q_f32(&p->f_mant[i + 4]);

        int32x4_t acc_p0 = vld1q_s32(&p->f_pwr[i + 0]);
        int32x4_t acc_p1 = vld1q_s32(&p->f_pwr[i + 4]);

        float32x4_t zmpy0 = vmulq_f32(enz0, acc_m0);
        float32x4_t zmpy1 = vmulq_f32(enz1, acc_m1);

        uint32x4_t zClearExp0 = vandq_u32(fnoexp, vreinterpretq_u32_f32(zmpy0));
        uint32x4_t zClearExp1 = vandq_u32(fnoexp, vreinterpretq_u32_f32(zmpy1));

        float32x4_t z0 = vreinterpretq_f32_u32(vorrq_u32(zClearExp0, fexp0));
        float32x4_t z1 = vreinterpretq_f32_u32(vorrq_u32(zClearExp1, fexp0));

        int32x4_t az0 = vshrq_n_s32(vreinterpretq_s32_f32(zmpy0), 23);
        int32x4_t az1 = vshrq_n_s32(vreinterpretq_s32_f32(zmpy1), 23);

        int32x4_t azsum0 = vaddq_s32(az0, acc_p0);
        int32x4_t azsum1 = vaddq_s32(az1, acc_p1);

        int32x4_t azc0 = vsubq_s32(azsum0, expcorr);
        int32x4_t azc1 = vsubq_s32(azsum1, expcorr);

        vst1q_f32(&p->f_mant[i + 0], z0);
        vst1q_f32(&p->f_mant[i + 4], z1);

        vst1q_s32(&p->f_pwr[i + 0], azc0);
        vst1q_s32(&p->f_pwr[i + 4], azc1);
    }
}

#undef TEMPLATE_FUNC_NAME
