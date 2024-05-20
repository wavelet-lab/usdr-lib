static
void TEMPLATE_FUNC_NAME(const void *__restrict indata,
                        unsigned indatabsz,
                        void *__restrict p_outdata,
                        unsigned outdatabsz)
{
    unsigned i = indatabsz;
    if ((outdatabsz / 2) < i)
        i = (outdatabsz / 2);

    const int16_t *in = (const int16_t *)indata;
    float* out = (float*)p_outdata;

    float32x4_t vf[4];

    while(i >= 32)
    {
        vf[0] = vcvtq_f32_s32( vmovl_s16(vld1_s16(in)) );
        vf[1] = vcvtq_f32_s32( vmovl_s16(vld1_s16(in + 4)) );
        vf[2] = vcvtq_f32_s32( vmovl_s16(vld1_s16(in + 8)) );
        vf[3] = vcvtq_f32_s32( vmovl_s16(vld1_s16(in + 12)) );

        vf[0] = vmulq_n_f32(vf[0], CONV_SCALE);
        vf[1] = vmulq_n_f32(vf[1], CONV_SCALE);
        vf[2] = vmulq_n_f32(vf[2], CONV_SCALE);
        vf[3] = vmulq_n_f32(vf[3], CONV_SCALE);

        vst1q_f32(out, vf[0]);
        vst1q_f32(out + 4,  vf[1]);
        vst1q_f32(out + 8,  vf[2]);
        vst1q_f32(out + 12, vf[3]);

        out += 16;
        in += 16;
        i -= 32;
    }

    while(i >= 16)
    {
        vf[0] = vcvtq_f32_s32( vmovl_s16(vld1_s16(in)) );
        vf[1] = vcvtq_f32_s32( vmovl_s16(vld1_s16(in + 4)) );

        vf[0] = vmulq_n_f32(vf[0], CONV_SCALE);
        vf[1] = vmulq_n_f32(vf[1], CONV_SCALE);

        vst1q_f32(out, vf[0]);
        vst1q_f32(out + 4, vf[1]);

        out += 8;
        in += 8;
        i -= 16;
    }

    while(i >= 8)
    {
        vf[0] = vcvtq_f32_s32( vmovl_s16(vld1_s16(in)) );
        vf[0] = vmulq_n_f32(vf[0], CONV_SCALE);
        vst1q_f32(out, vf[0]);

        out += 4;
        in += 4;
        i -= 8;
    }

    while(i)
    {
        *(out++) = *(in++) * CONV_SCALE;
        i -= 2;
    }
}

#undef TEMPLATE_FUNC_NAME
