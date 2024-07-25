static
void TEMPLATE_FUNC_NAME(fft_acc_t* __restrict p, uint16* __restrict d, unsigned fftsz)
{
    for (unsigned i = 0; i < fftsz; i += 16)
    {
        float32x4_t f0 = vcvtq_f32_u32( vmovl_u16(vld1_u16(&d[i +  0])) );
        float32x4_t f1 = vcvtq_f32_u32( vmovl_u16(vld1_u16(&d[i +  4])) );
        float32x4_t f2 = vcvtq_f32_u32( vmovl_u16(vld1_u16(&d[i +  8])) );
        float32x4_t f3 = vcvtq_f32_u32( vmovl_u16(vld1_u16(&d[i + 12])) );

        float32x4_t acc0 = vld1q_f32(&p->f_mant[i +  0]);
        float32x4_t acc1 = vld1q_f32(&p->f_mant[i +  4]);
        float32x4_t acc2 = vld1q_f32(&p->f_mant[i +  8]);
        float32x4_t acc3 = vld1q_f32(&p->f_mant[i + 12]);

        vst1q_f32(&p->f_mant[i +  0], vaddq_f32(acc0, f0));
        vst1q_f32(&p->f_mant[i +  4], vaddq_f32(acc1, f1));
        vst1q_f32(&p->f_mant[i +  8], vaddq_f32(acc2, f2));
        vst1q_f32(&p->f_mant[i + 12], vaddq_f32(acc3, f3));
    }
}

#undef TEMPLATE_FUNC_NAME
