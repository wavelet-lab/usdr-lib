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


#include "conv_i16_f32_i16_neon.inc"

    while(i >= 32)
    {
        vst1q_f32(out +  0, CONV_I16_F32(in +  0));
        vst1q_f32(out +  4, CONV_I16_F32(in +  4));
        vst1q_f32(out +  8, CONV_I16_F32(in +  8));
        vst1q_f32(out + 12, CONV_I16_F32(in + 12));

        out += 16;
        in += 16;
        i -= 32;
    }

    while(i >= 16)
    {
        vst1q_f32(out +  0, CONV_I16_F32(in +  0));
        vst1q_f32(out +  4, CONV_I16_F32(in +  4));

        out += 8;
        in += 8;
        i -= 16;
    }

    while(i >= 8)
    {
        vst1q_f32(out +  0, CONV_I16_F32(in +  0));

        out += 4;
        in += 4;
        i -= 8;
    }

    while(i >= 2)
    {
        *(out++) = *(in++) * CONV_SCALE;
        i -= 2;
    }
}

#undef TEMPLATE_FUNC_NAME
