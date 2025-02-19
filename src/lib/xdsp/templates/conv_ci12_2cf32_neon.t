static
void TEMPLATE_FUNC_NAME(const void *__restrict indata_p,
                        unsigned indatabsz,
                        void *__restrict outdata_0_p,
                        void *__restrict outdata_1_p,
                        unsigned outdatabsz)
{
    unsigned i = indatabsz;
    /* 12 bits -> 32 bits  =>  3 -> 8   */
    if ((outdatabsz * 3 / 8) < i)
        i = (outdatabsz * 3 / 8);

    const uint8_t* indata = (const uint8_t*)indata_p;
    float* outdata_0 = (float*)outdata_0_p;
    float* outdata_1 = (float*)outdata_1_p;

#include "conv_i12_i16_neon.inc"

#define CONVERT_CI12_2CF32_BLOCK(reg0, reg1) \
    {   \
        int16x8_t res0, res1; \
        CONVERT_I12_I16_BLOCK(reg0, reg1, res0, res1); \
        \
        uint64x2_t f0 = vreinterpretq_u64_f32(vmulq_n_f32(vcvtq_f32_s32(vmovl_s16(vget_low_s16(res0))), CONV_SCALE)); \
        uint64x2_t f1 = vreinterpretq_u64_f32(vmulq_n_f32(vcvtq_f32_s32(vmovl_s16(vget_high_s16(res0))), CONV_SCALE)); \
        uint64x2_t f2 = vreinterpretq_u64_f32(vmulq_n_f32(vcvtq_f32_s32(vmovl_s16(vget_low_s16(res1))), CONV_SCALE)); \
        uint64x2_t f3 = vreinterpretq_u64_f32(vmulq_n_f32(vcvtq_f32_s32(vmovl_s16(vget_high_s16(res1))), CONV_SCALE)); \
        \
        vst1q_u64((uint64_t*)(outdata_0 + 0), vcombine_u64(vget_low_u64(f0), vget_low_u64(f1))); \
        vst1q_u64((uint64_t*)(outdata_0 + 4), vcombine_u64(vget_low_u64(f2), vget_low_u64(f3))); \
        vst1q_u64((uint64_t*)(outdata_1 + 0), vcombine_u64(vget_high_u64(f0), vget_high_u64(f1))); \
        vst1q_u64((uint64_t*)(outdata_1 + 4), vcombine_u64(vget_high_u64(f2), vget_high_u64(f3))); \
        \
        outdata_0 += 8; \
        outdata_1 += 8; \
    }
// CONVERT_CI12_2CF32_BLOCK end

    uint8x16_t y0, y1, y2;

    if(i >= 48)
    {
        y0 = vld1q_u8(indata +  0);
        y1 = vld1q_u8(indata + 16);
        y2 = vld1q_u8(indata + 32);
        indata += 48;

        for(; i >= 2*48; i -= 48)
        {
            CONVERT_CI12_2CF32_BLOCK(y0, vcombine_u8(vget_high_u8(y0), vget_low_u8(y1)));
            CONVERT_CI12_2CF32_BLOCK(vcombine_u8(vget_high_u8(y1), vget_low_u8(y2)), y2);

            y0 = vld1q_u8(indata +  0);
            y1 = vld1q_u8(indata + 16);
            y2 = vld1q_u8(indata + 32);
            indata += 48;
        }

        i -= 48;

        CONVERT_CI12_2CF32_BLOCK(y0, vcombine_u8(vget_high_u8(y0), vget_low_u8(y1)));
        CONVERT_CI12_2CF32_BLOCK(vcombine_u8(vget_high_u8(y1), vget_low_u8(y2)), y2);
    }

#undef CONVERT_CI12_2CF32_BLOCK


    for (; i >= 6; i -= 6) {
        /* read 48 bits -> 4 floats */

        uint64_t v = *(const uint64_t *)indata;
        indata += 6;

        float a = (int16_t)(v << 4);
        float b = (int16_t)((v >> 8) & 0xfff0);
        float c = (int16_t)((v >> 20) & 0xfff0);
        float d = (int16_t)((v >> 32)  & 0xfff0);

        *(outdata_0++) = a * CONV_SCALE;
        *(outdata_0++) = b * CONV_SCALE;
        *(outdata_1++) = c * CONV_SCALE;
        *(outdata_1++) = d * CONV_SCALE;
    }

    float **dest = &outdata_0;

    while(i >= 3)
    {
        uint8_t v0 = *(indata++);
        uint8_t v1 = *(indata++);
        uint8_t v2 = *(indata++);
        i -= 3;

        float a = (int16_t) (((uint16_t)v0 << 4) | ((uint16_t)v1 << 12));
        float b = (int16_t) (((uint16_t)v2 << 8) | (v1 & 0xf0));

        *((*dest)++) = a * CONV_SCALE;
        *((*dest)++) = b * CONV_SCALE;

        dest = (*dest == outdata_0) ? &outdata_1 : &outdata_0;
    }

    if(i >= 2)
    {
        uint16_t v = *(const uint16_t*)indata;
        float a = (int16_t)(v << 4);
        *((*dest)++) = a * CONV_SCALE;
        i -= 2;
    }
}

#undef TEMPLATE_FUNC_NAME
