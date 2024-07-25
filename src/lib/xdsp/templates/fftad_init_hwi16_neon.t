static
void TEMPLATE_FUNC_NAME(fft_acc_t* __restrict p,  unsigned fftsz)
{
    const float32x4_t f = vdupq_n_f32(0.f);

    for (unsigned i = 0; i < fftsz; i += 16)
    {
        vst1q_f32(p->f_mant + i +  0, f);
        vst1q_f32(p->f_mant + i +  4, f);
        vst1q_f32(p->f_mant + i +  8, f);
        vst1q_f32(p->f_mant + i + 12, f);
    }
}

#undef TEMPLATE_FUNC_NAME
