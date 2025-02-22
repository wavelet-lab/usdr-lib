static
void TEMPLATE_FUNC_NAME(const void *__restrict indata_p,
                        unsigned indatabsz,
                        void *__restrict outdata_0_p,
                        void *__restrict outdata_1_p,
                        unsigned outdatabsz)
{
    unsigned i = indatabsz;
    /* 12 bits -> 32 bits  =>  3 -> 4   */
    if ((outdatabsz * 3 / 4) < i)
        i = (outdatabsz * 3 / 4);

    const uint8_t* indata = (const uint8_t*)indata_p;
    int16_t* outdata_0 = (int16_t*)outdata_0_p;
    int16_t* outdata_1 = (int16_t*)outdata_1_p;

#include "conv_i12_i16_neon.inc"

#define CONVERT_CI12_2CI16_BLOCK(reg0, reg1) \
    {   \
        int16x8_t res0, res1; \
        CONVERT_I12_I16_BLOCK(reg0, reg1, res0, res1); \
        \
        uint32x4x2_t rs = vuzpq_u32(vreinterpretq_u32_s16(res0), vreinterpretq_u32_s16(res1)); \
        vst1q_u32((uint32_t*)outdata_0, rs.val[0]); \
        vst1q_u32((uint32_t*)outdata_1, rs.val[1]); \
        \
        outdata_0 += 8; \
        outdata_1 += 8; \
    }
// CONVERT_CI12_2CI16_BLOCK end

    uint8x16_t y0, y1, y2;

    if(i >= 48)
    {
        y0 = vld1q_u8(indata +  0);
        y1 = vld1q_u8(indata + 16);
        y2 = vld1q_u8(indata + 32);
        indata += 48;

        for(; i >= 2*48; i -= 48)
        {
            CONVERT_CI12_2CI16_BLOCK(y0, vcombine_u8(vget_high_u8(y0), vget_low_u8(y1)));
            CONVERT_CI12_2CI16_BLOCK(vcombine_u8(vget_high_u8(y1), vget_low_u8(y2)), y2);

            y0 = vld1q_u8(indata +  0);
            y1 = vld1q_u8(indata + 16);
            y2 = vld1q_u8(indata + 32);
            indata += 48;
        }

        i -= 48;

        CONVERT_CI12_2CI16_BLOCK(y0, vcombine_u8(vget_high_u8(y0), vget_low_u8(y1)));
        CONVERT_CI12_2CI16_BLOCK(vcombine_u8(vget_high_u8(y1), vget_low_u8(y2)), y2);
    }

#undef CONVERT_CI12_2CI16_BLOCK

    for (; i >= 6; i -= 6)
    {
        /* read 48 bits -> 4 int16 (64 bits) */

        uint64_t v = *(const uint64_t *)indata;
        indata += 6;

        *(outdata_0++) = (int16_t)((v <<  4)         );
        *(outdata_0++) = (int16_t)((v >>  8) & 0xfff0);
        *(outdata_1++) = (int16_t)((v >> 20) & 0xfff0);
        *(outdata_1++) = (int16_t)((v >> 32) & 0xfff0);
    }
    // do nothing with tail
}

#undef TEMPLATE_FUNC_NAME
