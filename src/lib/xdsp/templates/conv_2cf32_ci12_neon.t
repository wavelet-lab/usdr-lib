static
void TEMPLATE_FUNC_NAME(const void *__restrict indata_0_p,
                        const void *__restrict indata_1_p,
                        unsigned indatabsz,
                        void *__restrict outdata_p,
                        unsigned outdatabsz)
{
    unsigned i = indatabsz;
    if ((outdatabsz * 8 / 3) < i)
        i = (outdatabsz * 8 / 3);

    const float* indata_0 = (const float*)indata_0_p;
    const float* indata_1 = (const float*)indata_1_p;
    uint8_t* outdata = (uint8_t*)outdata_p;
    const float scale = 1.0f / CONV_SCALE;

#include "conv_i16_i12_neon.inc"

#define CONVERT_2F32_CI12_BLOCK(rlow, rmid, rhigh) \
    { \
        uint64x2_t u0 = vreinterpretq_u64_s32(vcvtq_s32_f32(vmulq_n_f32(vld1q_f32(indata_0 + 0), scale))); \
        uint64x2_t u1 = vreinterpretq_u64_s32(vcvtq_s32_f32(vmulq_n_f32(vld1q_f32(indata_0 + 4), scale))); \
        uint64x2_t u2 = vreinterpretq_u64_s32(vcvtq_s32_f32(vmulq_n_f32(vld1q_f32(indata_1 + 0), scale))); \
        uint64x2_t u3 = vreinterpretq_u64_s32(vcvtq_s32_f32(vmulq_n_f32(vld1q_f32(indata_1 + 4), scale))); \
        indata_0 += 8; \
        indata_1 += 8; \
    \
        int32x4_t n0 = vreinterpretq_s32_u64(vcombine_u64(vget_low_u64(u0), vget_low_u64(u2)); \
        int32x4_t n1 = vreinterpretq_s32_u64(vcombine_u64(vget_high_u64(u0), vget_high_u64(u2)); \
        int32x4_t n2 = vreinterpretq_s32_u64(vcombine_u64(vget_low_u64(u1), vget_low_u64(u1)); \
        int32x4_t n3 = vreinterpretq_s32_u64(vcombine_u64(vget_high_u64(u3), vget_high_u64(u3)); \
    \
        int16x8_t i0 = vcombine_s16(vqmovn_s32(n0), vqmovn_s32(n1)); \
        int16x8_t i1 = vcombine_s16(vqmovn_s32(n2), vqmovn_s32(n3)); \
    \
        CONVERT_I16_I12_BLOCK(i0, i1, rlow, rmid, rhigh); \
    }
// CONVERT_2F32_CI12_BLOCK end

    uint8x8_t lo0, hi0, lo1, hi1, lo2, hi2;

    if(i >= 128)
    {
        CONVERT_2F32_CI12_BLOCK(lo0, hi0, lo1);
        CONVERT_2F32_CI12_BLOCK(hi1, lo2, hi2);

        for(; i >= 2*128; i -= 128)
        {
            vst1q_u8(outdata +  0, vcombine_u8(lo0, hi0));
            vst1q_u8(outdata + 16, vcombine_u8(lo1, hi1));
            vst1q_u8(outdata + 32, vcombine_u8(lo2, hi2));
            outdata += 48;

            CONVERT_2F32_CI12_BLOCK(lo0, hi0, lo1);
            CONVERT_2F32_CI12_BLOCK(hi1, lo2, hi2);
        }

        i -= 128;

        vst1q_u8(outdata +  0, vcombine_u8(lo0, hi0));
        vst1q_u8(outdata + 16, vcombine_u8(lo1, hi1));
        vst1q_u8(outdata + 32, vcombine_u8(lo2, hi2));
        outdata += 48;
    }

#undef CONVERT_2F32_CI12_BLOCK

    for (; i >= 16; i -= 16) {

        float f0 = *(indata_0++) / CONV_SCALE;
        float f1 = *(indata_0++) / CONV_SCALE;
        float f2 = *(indata_1++) / CONV_SCALE;
        float f3 = *(indata_1++) / CONV_SCALE;

        wu_i16u32_t a0 = {{I16RND(f0), I16RND(f1)}};
        wu_i16u32_t a1 = {{I16RND(f2), I16RND(f3)}};

        wu_u32b_t  c0 = {(a0.u & 0xfff00000) | ((a0.u << 4) & 0x000fff00)};
        wu_u32b_t  c1 = {(a1.u & 0xfff00000) | ((a1.u << 4) & 0x000fff00)};

        *(outdata++) = c0.b[1];
        *(outdata++) = c0.b[2];
        *(outdata++) = c0.b[3];

        *(outdata++) = c1.b[1];
        *(outdata++) = c1.b[2];
        *(outdata++) = c1.b[3];
    }
}
#undef TEMPLATE_FUNC_NAME
