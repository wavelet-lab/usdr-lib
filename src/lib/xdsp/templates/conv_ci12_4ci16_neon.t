static
void TEMPLATE_FUNC_NAME(const void *__restrict indata_p,
                        unsigned indatabsz,
                        void *__restrict outdata_0_p,
                        void *__restrict outdata_1_p,
                        void *__restrict outdata_2_p,
                        void *__restrict outdata_3_p,
                        unsigned outdatabsz)
{
    unsigned i = indatabsz;
    /* 12 bits -> 32 bits  =>  3 -> 4   */
    if ((outdatabsz * 3 / 4) < i)
        i = (outdatabsz * 3 / 4);

    const uint8_t* indata = (const uint8_t*)indata_p;
    int16_t* outdata_0 = (int16_t*)outdata_0_p;
    int16_t* outdata_1 = (int16_t*)outdata_1_p;
    int16_t* outdata_2 = (int16_t*)outdata_2_p;
    int16_t* outdata_3 = (int16_t*)outdata_3_p;

#include "conv_i12_i16_neon.inc"

#define CONVERT_CI12_4CI16_BLOCK(reg0, reg1, reg2, reg3) \
    {   \
        int16x8_t res0, res1, res2, res3; \
        CONVERT_I12_I16_BLOCK(reg0, reg1, res0, res1); \
        CONVERT_I12_I16_BLOCK(reg2, reg3, res2, res3); \
        \
        uint32x4x2_t trn0 = vtrnq_u32(vreinterpretq_u32_s16(res0), vreinterpretq_u32_s16(res1)); \
        uint32x4x2_t trn1 = vtrnq_u32(vreinterpretq_u32_s16(res2), vreinterpretq_u32_s16(res3)); \
        \
        vst1q_u32((uint32_t*)outdata_0, vcombine_u32(vget_low_u32 (trn0.val[0]), vget_low_u32 (trn1.val[0]))); \
        vst1q_u32((uint32_t*)outdata_1, vcombine_u32(vget_low_u32 (trn0.val[1]), vget_low_u32 (trn1.val[1]))); \
        vst1q_u32((uint32_t*)outdata_2, vcombine_u32(vget_high_u32(trn0.val[0]), vget_high_u32(trn1.val[0]))); \
        vst1q_u32((uint32_t*)outdata_3, vcombine_u32(vget_high_u32(trn0.val[1]), vget_high_u32(trn1.val[1]))); \
        \
        outdata_0 += 8; \
        outdata_1 += 8; \
        outdata_2 += 8; \
        outdata_3 += 8; \
    }
// CONVERT_CI12_4CI16_BLOCK end

    uint8x16_t y0, y1, y2, y3, y4, y5;

    if(i >= 96)
    {
        y0 = vld1q_u8(indata +  0);
        y1 = vld1q_u8(indata + 16);
        y2 = vld1q_u8(indata + 32);
        y3 = vld1q_u8(indata + 48);
        y4 = vld1q_u8(indata + 64);
        y5 = vld1q_u8(indata + 80);
        indata += 96;

        for(; i >= 2*96; i -= 96)
        {
            CONVERT_CI12_4CI16_BLOCK(y0, vcombine_u8(vget_high_u8(y0), vget_low_u8(y1)), vcombine_u8(vget_high_u8(y1), vget_low_u8(y2)), y2);
            CONVERT_CI12_4CI16_BLOCK(y3, vcombine_u8(vget_high_u8(y3), vget_low_u8(y4)), vcombine_u8(vget_high_u8(y4), vget_low_u8(y5)), y5);

            y0 = vld1q_u8(indata +  0);
            y1 = vld1q_u8(indata + 16);
            y2 = vld1q_u8(indata + 32);
            y3 = vld1q_u8(indata + 48);
            y4 = vld1q_u8(indata + 64);
            y5 = vld1q_u8(indata + 80);
            indata += 96;
        }

        i -= 96;

        CONVERT_CI12_4CI16_BLOCK(y0, vcombine_u8(vget_high_u8(y0), vget_low_u8(y1)), vcombine_u8(vget_high_u8(y1), vget_low_u8(y2)), y2);
        CONVERT_CI12_4CI16_BLOCK(y3, vcombine_u8(vget_high_u8(y3), vget_low_u8(y4)), vcombine_u8(vget_high_u8(y4), vget_low_u8(y5)), y5);
    }

#undef CONVERT_CI12_4CI16_BLOCK

    for (; i >= 12; i -= 12) {
        /* read 12 bytes -> 4ci16 */

        uint64_t v0 = *(const uint64_t *)(indata + 0);
        uint64_t v1 = *(const uint64_t *)(indata + 6);
        indata += 12;

        *(outdata_0++) = (int16_t)((v0 <<  4)         );
        *(outdata_0++) = (int16_t)((v0 >>  8) & 0xfff0);
        *(outdata_1++) = (int16_t)((v0 >> 20) & 0xfff0);
        *(outdata_1++) = (int16_t)((v0 >> 32) & 0xfff0);
        *(outdata_2++) = (int16_t)((v1 <<  4)         );
        *(outdata_2++) = (int16_t)((v1 >>  8) & 0xfff0);
        *(outdata_3++) = (int16_t)((v1 >> 20) & 0xfff0);
        *(outdata_3++) = (int16_t)((v1 >> 32) & 0xfff0);
    }

    // tail ignored
}

#undef TEMPLATE_FUNC_NAME
