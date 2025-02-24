static
void TEMPLATE_FUNC_NAME(const void *__restrict indata_0_p,
                        const void *__restrict indata_1_p,
                        const void *__restrict indata_2_p,
                        const void *__restrict indata_3_p,
                        unsigned indatabsz,
                        void *__restrict outdata_p,
                        unsigned outdatabsz)
{
    unsigned i = indatabsz;
    if ((outdatabsz * 4 / 3) < i)
        i = (outdatabsz * 4 / 3);

    const int16_t* indata_0 = (const int16_t*)indata_0_p;
    const int16_t* indata_1 = (const int16_t*)indata_1_p;
    const int16_t* indata_2 = (const int16_t*)indata_2_p;
    const int16_t* indata_3 = (const int16_t*)indata_3_p;

    uint8_t* outdata = (uint8_t*)outdata_p;

#include "conv_i16_i12_neon.inc"

#define CONVERT_4CI16_CI12_BLOCK(rlow, rmid, rhigh) \
    { \
        uint32x4_t ci0 = vld1q_u32((uint32_t*)indata_0); \
        uint32x4_t ci1 = vld1q_u32((uint32_t*)indata_1); \
        uint32x4_t ci2 = vld1q_u32((uint32_t*)indata_2); \
        uint32x4_t ci3 = vld1q_u32((uint32_t*)indata_3); \
        indata_0 += 8; \
        indata_1 += 8; \
        indata_2 += 8; \
        indata_3 += 8; \
        \
        uint32x4x2_t trn0 = vtrnq_u32(ci0, ci1); \
        uint32x4x2_t trn1 = vtrnq_u32(ci2, ci3); \
        \
        int16x8_t i0 = vreinterpretq_s16_u32(vcombine_u32(vget_low_u32 (trn0.val[0]), vget_low_u32 (trn1.val[0]))); \
        int16x8_t i1 = vreinterpretq_s16_u32(vcombine_u32(vget_low_u32 (trn0.val[1]), vget_low_u32 (trn1.val[1]))); \
        int16x8_t i2 = vreinterpretq_s16_u32(vcombine_u32(vget_high_u32(trn0.val[0]), vget_high_u32(trn1.val[0]))); \
        int16x8_t i3 = vreinterpretq_s16_u32(vcombine_u32(vget_high_u32(trn0.val[1]), vget_high_u32(trn1.val[1]))); \
        \
        CONVERT_I16_I12_BLOCK(i0, i1, rlow.val[0], rmid.val[0], rhigh.val[0]); \
        CONVERT_I16_I12_BLOCK(i2, i3, rlow.val[1], rmid.val[1], rhigh.val[1]); \
    }
// CONVERT_4CI16_CI12_BLOCK end

    uint8x8x2_t a, b, c, d, e, f;

    if(i >= 128)
    {
        CONVERT_4CI16_CI12_BLOCK(a, b, c);
        CONVERT_4CI16_CI12_BLOCK(d, e, f);

        for(; i >= 2*128; i -= 128)
        {
            vst1q_u8(outdata +  0, vcombine_u8(a.val[0], b.val[0]));
            vst1q_u8(outdata + 16, vcombine_u8(c.val[0], a.val[1]));
            vst1q_u8(outdata + 32, vcombine_u8(b.val[1], c.val[1]));
            vst1q_u8(outdata + 48, vcombine_u8(d.val[0], e.val[0]));
            vst1q_u8(outdata + 64, vcombine_u8(f.val[0], d.val[1]));
            vst1q_u8(outdata + 80, vcombine_u8(e.val[1], f.val[1]));
            outdata += 96;

            CONVERT_4CI16_CI12_BLOCK(a, b, c);
            CONVERT_4CI16_CI12_BLOCK(d, e, f);
        }

        i -= 128;

        vst1q_u8(outdata +  0, vcombine_u8(a.val[0], b.val[0]));
        vst1q_u8(outdata + 16, vcombine_u8(c.val[0], a.val[1]));
        vst1q_u8(outdata + 32, vcombine_u8(b.val[1], c.val[1]));
        vst1q_u8(outdata + 48, vcombine_u8(d.val[0], e.val[0]));
        vst1q_u8(outdata + 64, vcombine_u8(f.val[0], d.val[1]));
        vst1q_u8(outdata + 80, vcombine_u8(e.val[1], f.val[1]));
        outdata += 96;
    }

#undef CONVERT_4CI16_CI12_BLOCK

    for (; i >= 16; i -= 16) {

        const int16_t i0 = *indata_0++;
        const int16_t q0 = *indata_0++;
        const int16_t i1 = *indata_1++;
        const int16_t q1 = *indata_1++;
        const int16_t i2 = *indata_2++;
        const int16_t q2 = *indata_2++;
        const int16_t i3 = *indata_3++;
        const int16_t q3 = *indata_3++;

        wu_i16u32_t a0 = {{i0, q0}};
        wu_i16u32_t a1 = {{i1, q1}};
        wu_i16u32_t a2 = {{i2, q2}};
        wu_i16u32_t a3 = {{i3, q3}};

        wu_u32b_t  c0 = {(a0.u & 0xfff00000) | ((a0.u << 4) & 0x000fff00)};
        wu_u32b_t  c1 = {(a1.u & 0xfff00000) | ((a1.u << 4) & 0x000fff00)};
        wu_u32b_t  c2 = {(a2.u & 0xfff00000) | ((a2.u << 4) & 0x000fff00)};
        wu_u32b_t  c3 = {(a3.u & 0xfff00000) | ((a3.u << 4) & 0x000fff00)};

        const wu_u32b_t arr[] = {c0, c1, c2, c3};
        for(unsigned j = 0; j < 4; ++j)
        {
            *(outdata++) = arr[j].b[1];
            *(outdata++) = arr[j].b[2];
            *(outdata++) = arr[j].b[3];
        }
    }
}
#undef TEMPLATE_FUNC_NAME
