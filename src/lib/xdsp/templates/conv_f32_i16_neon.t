static
void TEMPLATE_FUNC_NAME(const void *__restrict indata_p,
                        unsigned indatabsz,
                        void *__restrict outdata_p,
                        unsigned outdatabsz)
{
    unsigned i = indatabsz;
    if ((outdatabsz * 2) < i)
        i = (outdatabsz * 2);

    const float* indata = (const float*)indata_p;
    int16_t* outdata = (int16_t*)outdata_p;
    const float scale = 1.0f / CONV_SCALE;

#include "conv_i16_f32_i16_neon.inc"

    for(; i >= 64; i -= 64)
    {
        CONV_F32_I16(vld1q_f32(indata + 0), vld1q_f32(indata +  4), outdata + 0);
        CONV_F32_I16(vld1q_f32(indata + 8), vld1q_f32(indata + 12), outdata + 8);
        indata += 16;
        outdata += 16;
    }

    for(; i >= 32; i -= 32)
    {
        CONV_F32_I16(vld1q_f32(indata), vld1q_f32(indata + 4), outdata);
        indata += 8;
        outdata += 8;
    }

    for (; i >= 16; i -= 16, indata += 4, outdata += 4) {
        int16_t a = indata[0] / CONV_SCALE;
        int16_t b = indata[1] / CONV_SCALE;
        int16_t c = indata[2] / CONV_SCALE;
        int16_t d = indata[3] / CONV_SCALE;

        uint64_t v = (uint64_t)(uint16_t)a | ((uint64_t)(uint16_t)b << 16) | ((uint64_t)(uint16_t)c << 32) | ((uint64_t)(uint16_t)d << 48);
        *(int64_t*)outdata = v;
    }

    for (; i >= 4; i -= 4) {
        *(outdata++) = *(indata++) / CONV_SCALE;
    }
}

#undef TEMPLATE_FUNC_NAME
