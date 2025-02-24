static
void TEMPLATE_FUNC_NAME(const void *__restrict indata_p,
                        unsigned indatabsz,
                        void *__restrict outdata_p,
                        unsigned outdatabsz)
{
    unsigned i = indatabsz;
    /* 12 bits -> 16 bits  =>  3 -> 4   */
    if ((outdatabsz * 3 / 4) < i)
        i = (outdatabsz * 3 / 4);

    const uint8_t* indata = (const uint8_t*)indata_p;
    int16_t* outdata = (int16_t*)outdata_p;

#include "conv_i12_i16_neon.inc"

#define CONVERT_I12_i16_BLOCK2(reg0, reg1) \
    {   \
        int16x8_t res0, res1; \
        CONVERT_I12_I16_BLOCK(reg0, reg1, res0, res1); \
        vst1q_s16(outdata + 0, res0); \
        vst1q_s16(outdata + 8, res1); \
        \
        outdata += 16; \
    }
// CONVERT_I12_i16_BLOCK2 end

    uint8x16_t y0, y1, y2;

    if(i >= 48)
    {
        y0 = vld1q_u8(indata +  0);
        y1 = vld1q_u8(indata + 16);
        y2 = vld1q_u8(indata + 32);
        indata += 48;

        for(; i >= 2*48; i -= 48)
        {
            CONVERT_I12_i16_BLOCK2(y0, vcombine_u8(vget_high_u8(y0), vget_low_u8(y1)));
            CONVERT_I12_i16_BLOCK2(vcombine_u8(vget_high_u8(y1), vget_low_u8(y2)), y2);

            y0 = vld1q_u8(indata +  0);
            y1 = vld1q_u8(indata + 16);
            y2 = vld1q_u8(indata + 32);
            indata += 48;
        }

        i -= 48;

        CONVERT_I12_i16_BLOCK2(y0, vcombine_u8(vget_high_u8(y0), vget_low_u8(y1)));
        CONVERT_I12_i16_BLOCK2(vcombine_u8(vget_high_u8(y1), vget_low_u8(y2)), y2);
    }

#undef CONVERT_I12_F32_BLOCK2

    while(i >= 3)
    {
        const uint8_t v0 = *(indata++);
        const uint8_t v1 = *(indata++);
        const uint8_t v2 = *(indata++);
        i -= 3;

        const int16_t a = (int16_t) (((uint16_t)v0 << 4) | ((uint16_t)v1 << 12));
        const int16_t b = (int16_t) (((uint16_t)v2 << 8) | (v1 & 0xf0));

        *(outdata++) = a;
        *(outdata++) = b;
    }

    if(i >= 2)
    {
        const uint16_t v = *(const uint16_t*)indata;
        const int16_t a = (int16_t)(v << 4);
        *(outdata++) = a;
        i -= 2;
    }
}

#undef TEMPLATE_FUNC_NAME
