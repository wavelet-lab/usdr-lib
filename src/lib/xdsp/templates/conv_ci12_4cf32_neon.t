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
    /* 12 bits -> 32 bits  =>  3 -> 8   */
    if ((outdatabsz * 3 / 8) < i)
        i = (outdatabsz * 3 / 8);

    const uint8_t* indata = (const uint8_t*)indata_p;
    float* outdata_0 = (float*)outdata_0_p;
    float* outdata_1 = (float*)outdata_1_p;
    float* outdata_2 = (float*)outdata_2_p;
    float* outdata_3 = (float*)outdata_3_p;

#include "conv_i12_i16_neon.inc"

#define CONVERT_CI12_4CF32_BLOCK(reg0, reg1) \
    {   \
        int16x8_t res0, res1; \
        CONVERT_I12_I16_BLOCK(reg0, reg1, res0, res1); \
        \
        uint64x2_t cf0 = vreinterpretq_u64_f32(vmulq_n_f32(vcvtq_f32_s32(vmovl_s16(vget_low_s16(res0))), CONV_SCALE)); \
        uint64x2_t cf1 = vreinterpretq_u64_f32(vmulq_n_f32(vcvtq_f32_s32(vmovl_s16(vget_high_s16(res0))), CONV_SCALE)); \
        uint64x2_t cf2 = vreinterpretq_u64_f32(vmulq_n_f32(vcvtq_f32_s32(vmovl_s16(vget_low_s16(res1))), CONV_SCALE)); \
        uint64x2_t cf3 = vreinterpretq_u64_f32(vmulq_n_f32(vcvtq_f32_s32(vmovl_s16(vget_high_s16(res1))), CONV_SCALE)); \
        \
        vst1q_u64((uint64_t*)(outdata_0), vcombine_u64(vget_low_u64(cf0), vget_low_u64(cf2))); \
        vst1q_u64((uint64_t*)(outdata_1), vcombine_u64(vget_high_u64(cf0), vget_high_u64(cf2))); \
        vst1q_u64((uint64_t*)(outdata_2), vcombine_u64(vget_low_u64(cf1), vget_low_u64(cf3))); \
        vst1q_u64((uint64_t*)(outdata_3), vcombine_u64(vget_high_u64(cf1), vget_high_u64(cf3))); \
        \
        outdata_0 += 4; \
        outdata_1 += 4; \
        outdata_2 += 4; \
        outdata_3 += 4; \
    }
// CONVERT_CI12_4CF32_BLOCK end

    uint8x16_t y0, y1, y2;

    if(i >= 48)
    {
        y0 = vld1q_u8(indata +  0);
        y1 = vld1q_u8(indata + 16);
        y2 = vld1q_u8(indata + 32);
        indata += 48;

        for(; i >= 2*48; i -= 48)
        {
            CONVERT_CI12_4CF32_BLOCK(y0, vcombine_u8(vget_high_u8(y0), vget_low_u8(y1)));
            CONVERT_CI12_4CF32_BLOCK(vcombine_u8(vget_high_u8(y1), vget_low_u8(y2)), y2);

            y0 = vld1q_u8(indata +  0);
            y1 = vld1q_u8(indata + 16);
            y2 = vld1q_u8(indata + 32);
            indata += 48;
        }

        i -= 48;

        CONVERT_CI12_4CF32_BLOCK(y0, vcombine_u8(vget_high_u8(y0), vget_low_u8(y1)));
        CONVERT_CI12_4CF32_BLOCK(vcombine_u8(vget_high_u8(y1), vget_low_u8(y2)), y2);
    }

#undef CONVERT_CI12_4CF32_BLOCK

    for (; i >= 12; i -= 12) {
        /* read 12 bytes -> 2*48 bits -> 4*2 floats -> 4cf32 */

        uint64_t v0 = *(const uint64_t *)(indata + 0);
        uint64_t v1 = *(const uint64_t *)(indata + 6);
        indata += 12;

        float i0 = (int16_t)(v0 << 4);
        float q0 = (int16_t)((v0 >> 8) & 0xfff0);
        float i1 = (int16_t)((v0 >> 20) & 0xfff0);
        float q1 = (int16_t)((v0 >> 32)  & 0xfff0);
        float i2 = (int16_t)(v1 << 4);
        float q2 = (int16_t)((v1 >> 8) & 0xfff0);
        float i3 = (int16_t)((v1 >> 20) & 0xfff0);
        float q3 = (int16_t)((v1 >> 32)  & 0xfff0);

        *(outdata_0++) = i0 * CONV_SCALE;
        *(outdata_0++) = q0 * CONV_SCALE;
        *(outdata_1++) = i1 * CONV_SCALE;
        *(outdata_1++) = q1 * CONV_SCALE;
        *(outdata_2++) = i2 * CONV_SCALE;
        *(outdata_2++) = q2 * CONV_SCALE;
        *(outdata_3++) = i3 * CONV_SCALE;
        *(outdata_3++) = q3 * CONV_SCALE;
    }

    // tail ignored

}

#undef TEMPLATE_FUNC_NAME
