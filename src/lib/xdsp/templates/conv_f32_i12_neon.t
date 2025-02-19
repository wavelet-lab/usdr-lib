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

    const uint64x2_t maske = vdupq_n_u64(0x0000fff00000fff0);
    const uint64x2_t masko = vdupq_n_u64(0xfff00000fff00000);

    const uint8x16_t lk0 = {0x01,0x02,0x03, 0x05,0x06,0x07, 0x09,0x0a,0x0b, 0x0d,0x0e,0x0f, 0x80,0x80,0x80,0x80};
    const uint8x16_t lk1 = {0x80,0x80,0x80,0x80, 0x01,0x02,0x03, 0x05,0x06,0x07, 0x09,0x0a,0x0b, 0x0d,0x0e,0x0f};

/*
*  |              (2)              |              (1)              |              (0)              |
*  +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
*  | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 |
*  +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
*
*  | f7  |0| f6  |0| f5  |0| f4  |0| f3  |0| f2  |0| f1  |0| f0  |0|        s0
*  | f15 |0| f14 |0| f13 |0| f12 |0| f11 |0| f10 |0| f9  |0| f8  |0|        s1
*
*  |00000|0| f6  |0|00000|0| f4  |0|00000|0| f2  |0|00000|0| f0  |0|        s0_1
*  |00000| f6  |0|00000|0| f4  |0|00000|0| f2  |0|00000|0| f0  |0|0|        s0_1 << 4
*
*  | f7  |0|00000|0| f5  |0|00000|0| f3  |0|00000|0| f1  |0|00000|0|        s0_2
*
*  | f7  | f6  |0|0| f5  | f4  |0|0| f3  | f2  |0|0| f1  | f0  |0|0|        s0_1|s0_2
*  | f15 | f14 |0|0| f13 | f12 |0|0| f11 | f10 |0|0| f9  | f8  |0|0|        s1_1|s1_2
*  +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
*  | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 |
*  +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
*  | f   e   d   c   b   a   9   8   7   6   5   4   3   2   1   0
*
*  |              (2)              |              (1)              |              (0)              |
*  +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
*  | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 |
*  +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
*  |               | f7  | f6  | f5  | f4  | f3  | f2  | f1  | f0  |        res0
*  | f15 | f14 | f13 | f12 | f11 | f10 | f9  | f8  |               |        res1
*
*/

#define CONVERT_F32_I12_BLOCK(res0, res1) \
    { \
        int32x4_t n0 = vcvtq_s32_f32(vmulq_n_f32(vld1q_f32(indata +  0), scale)); \
        int32x4_t n1 = vcvtq_s32_f32(vmulq_n_f32(vld1q_f32(indata +  4), scale)); \
        int32x4_t n2 = vcvtq_s32_f32(vmulq_n_f32(vld1q_f32(indata +  8), scale)); \
        int32x4_t n3 = vcvtq_s32_f32(vmulq_n_f32(vld1q_f32(indata + 12), scale)); \
        indata += 16; \
    \
        uint64x2_t s0 = vreinterpretq_u64_s16(vcombine_s16(vqmovn_s32(n0), vqmovn_s32(n1))); \
        uint64x2_t s1 = vreinterpretq_u64_s16(vcombine_s16(vqmovn_s32(n2), vqmovn_s32(n3))); \
    \
        uint64x2_t s0_1 = vshlq_n_u64(vandq_u64(s0, maske), 4); \
        uint64x2_t s0_2 = vandq_u64(s0, masko); \
                   res0 = vreinterpretq_u8_u64(vorrq_u64(s0_1, s0_2)); \
    \
        uint64x2_t s1_1 = vshlq_n_u64(vandq_u64(s1, maske), 4); \
        uint64x2_t s1_2 = vandq_u64(s1, masko); \
                   res1 = vreinterpretq_u8_u64(vorrq_u64(s1_1, s1_2)); \
    \
        res0 = vqtbl1q_u8(res0, lk0); \
        res1 = vqtbl1q_u8(res1, lk1); \
    }
// CONVERT_F32_I12_BLOCK end

    uint8x16_t i0, i1, i2, i3;

    for (; i >= 128; i -= 128)
    {
        CONVERT_F32_I12_BLOCK(i0, i1);
        CONVERT_F32_I12_BLOCK(i2, i3);

        uint8x8_t lo0 = vget_low_u8(i0);
        uint8x8_t hi0 = vorr_u8(vget_high_u8(i0), vget_low_u8(i1));
        uint8x8_t lo1 = vget_high_u8(i1);

        uint8x8_t hi1 = vget_low_u8(i2);
        uint8x8_t lo2 = vorr_u8(vget_high_u8(i2), vget_low_u8(i3));
        uint8x8_t hi2 = vget_high_u8(i3);

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
