static
void TEMPLATE_FUNC_NAME(const void *__restrict indata_p,
                        unsigned indatabsz,
                        void *__restrict outdata_p,
                        unsigned outdatabsz)
{
    unsigned i = indatabsz;
    if ((outdatabsz * 8 / 3) < i)
        i = (outdatabsz * 8 / 3);

    const float *indata = (const float*)indata_p;
    uint8_t *outdata = (uint8_t*)outdata_p;
    const float scale = 1.0f / CONV_SCALE;

#include "conv_i16_i12_neon.inc"

#define CONVERT_F32_I12_BLOCK(rlow, rmid, rhigh) \
    { \
        int32x4_t n0 = vcvtq_s32_f32(vmulq_n_f32(vld1q_f32(indata +  0), scale)); \
        int32x4_t n1 = vcvtq_s32_f32(vmulq_n_f32(vld1q_f32(indata +  4), scale)); \
        int32x4_t n2 = vcvtq_s32_f32(vmulq_n_f32(vld1q_f32(indata +  8), scale)); \
        int32x4_t n3 = vcvtq_s32_f32(vmulq_n_f32(vld1q_f32(indata + 12), scale)); \
        indata += 16; \
    \
        int16x8_t i0 = vcombine_s16(vqmovn_s32(n0), vqmovn_s32(n1)); \
        int16x8_t i1 = vcombine_s16(vqmovn_s32(n2), vqmovn_s32(n3)); \
    \
        CONVERT_I16_I12_BLOCK(i0, i1, rlow, rmid, rhigh); \
    }
// CONVERT_F32_I12_BLOCK end

    uint8x8_t lo0, hi0, lo1, hi1, lo2, hi2;

    if(i >= 128)
    {
        CONVERT_F32_I12_BLOCK(lo0, hi0, lo1);
        CONVERT_F32_I12_BLOCK(hi1, lo2, hi2);

        for(; i >= 2*128; i -= 128)
        {
            vst1q_u8(outdata +  0, vcombine_u8(lo0, hi0));
            vst1q_u8(outdata + 16, vcombine_u8(lo1, hi1));
            vst1q_u8(outdata + 32, vcombine_u8(lo2, hi2));
            outdata += 48;

            CONVERT_F32_I12_BLOCK(lo0, hi0, lo1);
            CONVERT_F32_I12_BLOCK(hi1, lo2, hi2);
        }

        i -= 128;

        vst1q_u8(outdata +  0, vcombine_u8(lo0, hi0));
        vst1q_u8(outdata + 16, vcombine_u8(lo1, hi1));
        vst1q_u8(outdata + 32, vcombine_u8(lo2, hi2));
        outdata += 48;
    }

#undef CONVERT_F32_I12_BLOCK

#undef I16RND
#define I16RND(x) x > 0 ? (int16_t)(x + 0.5f) : (int16_t)(x - 0.5f)

    for (; i >= 8; i -= 8) {

        float f0 = *(indata++) / CONV_SCALE;
        float f1 = *(indata++) / CONV_SCALE;

        wu_i16u32_t a = {{I16RND(f0), I16RND(f1)}};
        wu_u32b_t   c = {(a.u & 0xfff00000) | ((a.u << 4) & 0x000fff00)};

        *(outdata++) = c.b[1];
        *(outdata++) = c.b[2];
        *(outdata++) = c.b[3];
    }

    if(i >= 4)
    {
        float f = *indata / CONV_SCALE;
        wu_i16b_t c = {I16RND(f)};

        *(outdata++) = c.b[0];
        *(outdata++) = c.b[1] >> 4;
        i -= 4;
    }
}

#undef TEMPLATE_FUNC_NAME
