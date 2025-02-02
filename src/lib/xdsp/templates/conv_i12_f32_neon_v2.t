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

    uint8x8x4_t y;
    y.val[3] = {0,0,0,0,0,0,0,0};

    const uint8x16_t lk0 = {0x0b, 0x0a, 0x09, 0x80, 0x08, 0x07, 0x06, 0x80, 0x05, 0x04, 0x03, 0x80, 0x02, 0x01, 0x00, 0x80};
    const uint8x16_t lk1 = {0x17, 0x16, 0x15, 0x80, 0x14, 0x13, 0x12, 0x80, 0x11, 0x10, 0x0f, 0x80, 0x0e, 0x0d, 0x0c, 0x80};

    const uint64x2_t mask0 = vld1q_dup_u64(0xfff00000fff00000);
    const uint64x2_t mask1 = vld1q_dup_u64(0x0000fff00000fff0);

/*
*  in:
*  |              (2)              |              (1)              |              (0)              |
*  +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
*  | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 |
*  +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
*  | f15 | f14 | f13 | f12 | f11 | f10 | f9  | f8  | f7  | f6  | f5  | f4  | f3  | f2  | f1  | f0  |
*  +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
*  | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 | 15| 14| 13| 12| 11| 10| 9 | 8 | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 |
*  +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
*  | 17| 16| 15| 14| 13| 12| 11| 10| 0f| 0e| 0d| 0c| 0b| 0a| 9 | 8 | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 |
*
*  | f7  | f6  | 0 | f5  | f4  | 0 | f3  | f2  | 0 | f1  | f0  | 0 |    r0
*  | f15 | f14 | 0 | f13 | f12 | 0 | f11 | f10 | 0 | f9  | f8  | 0 |    r1
*
*  |                         out.val[1]                            |                           out.val[0]                          |
*  | f15 |0| f14 |0| f13 |0| f12 |0| f11 |0| f10 |0| f9  |0| f8  |0| f7  |0| f6  |0| f5  |0| f4  |0| f3  |0| f2  |0| f1  |0| f0  |0|
*
*/


#define CONVERT_I12_F32_BLOCK(reg) \
    {   \
        uint8x16x2* preg = (uint8x16x2*)(&reg); \
        \
        uint64x2_t r0 = vreinterpretq_u64_u8(vqtbl2q_u8(*preg, lk0)); \
        uint64x2_t r1 = vreinterpretq_u64_u8(vqtbl2q_u8(*preg, lk1)); \
        \
        uint64x2_t r0_0 = vandq_u64(r0, mask0); \
        uint64x2_t r0_1 = vandq_u64(vshrq_n_u64(r0, 4), mask1); \
        int16x8_t  res0 = vreinterpretq_s16_u64(vorrq_u64(r0_0, r0_1)); \
        \
        uint64x2_t r1_0 = vandq_u64(r1, mask0); \
        uint64x2_t r1_1 = vandq_u64(vshrq_n_u64(r1, 4), mask1); \
        int16x8_t  res1 = vreinterpretq_s16_u64(vorrq_u64(r1_0, r1_1)); \
        \
        vst1q_f32(out +  0, vmulq_n_f32(vcvtq_f32_s32(vmovl_s16(vget_low_s16(res0))), CONV_SCALE)); \
        vst1q_f32(out +  4, vmulq_n_f32(vcvtq_f32_s32(vmovl_s16(vget_high_s16(res0))), CONV_SCALE)); \
        vst1q_f32(out +  8, vmulq_n_f32(vcvtq_f32_s32(vmovl_s16(vget_low_s16(res1))), CONV_SCALE)); \
        vst1q_f32(out + 12, vmulq_n_f32(vcvtq_f32_s32(vmovl_s16(vget_high_s16(res1))), CONV_SCALE)); \
        \
        out += 16; \
    }
// CONVERT_I12_F32_BLOCK end


    if(i >= 24)
    {
        y.val[0] = vld1_u8(in +  0);
        y.val[1] = vld1_u8(in +  8);
        y.val[2] = vld1_u8(in + 16);
        in += 24;

        for(; i >= 2*24; i -= 24)
        {
            CONVERT_I12_F32_BLOCK(y);

            y.val[0] = vld1_u8(in +  0);
            y.val[1] = vld1_u8(in +  8);
            y.val[2] = vld1_u8(in + 16);
            in += 24;
        }

        i -= 24;

        CONVERT_I12_F32_BLOCK(y);
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
