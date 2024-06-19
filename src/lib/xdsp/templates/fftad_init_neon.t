static
void TEMPLATE_FUNC_NAME(fft_acc_t* __restrict p,  unsigned fftsz)
{
    const float32x4_t e1 = vdupq_n_f32(1.0);
    const int32x4_t d1   = vdupq_n_s32(0);

    for (unsigned i = 0; i < fftsz; i += 8)
    {
        vst1q_f32(p->f_mant + i + 0, e1);
        vst1q_f32(p->f_mant + i + 4, e1);
        vst1q_s32(p->f_pwr  + i + 0, d1);
        vst1q_s32(p->f_pwr  + i + 4, d1);
    }
}

#undef TEMPLATE_FUNC_NAME
