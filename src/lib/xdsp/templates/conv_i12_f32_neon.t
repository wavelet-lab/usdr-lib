static
void TEMPLATE_FUNC_NAME(const void *__restrict indata_p,
                        unsigned indatabsz,
                        void *__restrict outdata_p,
                        unsigned outdatabsz)
{
    unsigned i = indatabsz;
    /* 12 bits -> 32 bits  =>  3 -> 8   */
    if ((outdatabsz * 3 / 8) < i)
        i = (outdatabsz * 3 / 8);

    const uint8_t *in = (const uint8_t*)indata_p;
    float* out = (float*)outdata_p;

    uint8x16_t y0, y1, y2;

#include "conv_i12_i16_neon.inc"

#define CONVERT_I12_F32_BLOCK(reg0, reg1) \
    {   \
        int16x8_t res0, res1; \
        CONVERT_I12_I16_BLOCK(reg0, reg1, res0, res1); \
        vst1q_f32(out +  0, vmulq_n_f32(vcvtq_f32_s32(vmovl_s16(vget_low_s16(res0))), CONV_SCALE)); \
        vst1q_f32(out +  4, vmulq_n_f32(vcvtq_f32_s32(vmovl_s16(vget_high_s16(res0))), CONV_SCALE)); \
        vst1q_f32(out +  8, vmulq_n_f32(vcvtq_f32_s32(vmovl_s16(vget_low_s16(res1))), CONV_SCALE)); \
        vst1q_f32(out + 12, vmulq_n_f32(vcvtq_f32_s32(vmovl_s16(vget_high_s16(res1))), CONV_SCALE)); \
        \
        out += 16; \
    }
// CONVERT_I12_F32_BLOCK end


    if(i >= 48)
    {
        y0 = vld1q_u8(in +  0);
        y1 = vld1q_u8(in + 16);
        y2 = vld1q_u8(in + 32);
        in += 48;

        for(; i >= 2*48; i -= 48)
        {
            CONVERT_I12_F32_BLOCK(y0, vcombine_u8(vget_high_u8(y0), vget_low_u8(y1)));
            CONVERT_I12_F32_BLOCK(vcombine_u8(vget_high_u8(y1), vget_low_u8(y2)), y2);

            y0 = vld1q_u8(in +  0);
            y1 = vld1q_u8(in + 16);
            y2 = vld1q_u8(in + 32);
            in += 48;
        }

        i -= 48;

        CONVERT_I12_F32_BLOCK(y0, vcombine_u8(vget_high_u8(y0), vget_low_u8(y1)));
        CONVERT_I12_F32_BLOCK(vcombine_u8(vget_high_u8(y1), vget_low_u8(y2)), y2);
    }

#undef CONVERT_I12_F32_BLOCK

    const uint8_t *indata = in;

    while(i >= 3)
    {
        uint8_t v0 = *(indata++);
        uint8_t v1 = *(indata++);
        uint8_t v2 = *(indata++);
        i -= 3;

        float a = (int16_t) (((uint16_t)v0 << 4) | ((uint16_t)v1 << 12));
        float b = (int16_t) (((uint16_t)v2 << 8) | (v1 & 0xf0));

        *(out++) = a * CONV_SCALE;
        *(out++) = b * CONV_SCALE;
    }

    if(i >= 2)
    {
        uint16_t v = *(const uint16_t*)indata;
        float a = (int16_t)(v << 4);
        *(out++) = a * CONV_SCALE;
        i -= 2;
    }
}
#undef TEMPLATE_FUNC_NAME
