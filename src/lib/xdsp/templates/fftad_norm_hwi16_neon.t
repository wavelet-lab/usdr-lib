static
void TEMPLATE_FUNC_NAME(fft_acc_t* __restrict p, unsigned fftsz, float scale, float corr, float* __restrict outa)
{
    const float32x4_t vcorr = vdupq_n_f32(corr / HWI16_SCALE_COEF + HWI16_CORR_COEF);
    scale /= HWI16_SCALE_COEF;

    for(unsigned i = 0; i < fftsz; i += 16)
    {
        float32x4_t acc0 = vld1q_f32(p->f_mant + i +  0);
        float32x4_t acc1 = vld1q_f32(p->f_mant + i +  4);
        float32x4_t acc2 = vld1q_f32(p->f_mant + i +  8);
        float32x4_t acc3 = vld1q_f32(p->f_mant + i + 12);

        float32x4_t f0 = vmlaq_n_f32(vcorr, acc0, scale);
        float32x4_t f1 = vmlaq_n_f32(vcorr, acc1, scale);
        float32x4_t f2 = vmlaq_n_f32(vcorr, acc2, scale);
        float32x4_t f3 = vmlaq_n_f32(vcorr, acc3, scale);

        vst1q_f32(outa + i +  0, f0);
        vst1q_f32(outa + i +  4, f1);
        vst1q_f32(outa + i +  8, f2);
        vst1q_f32(outa + i + 12, f3);
    }
}

#undef TEMPLATE_FUNC_NAME
