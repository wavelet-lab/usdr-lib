static
void TEMPLATE_FUNC_NAME(const void *__restrict indata_0_p,
                        const void *__restrict indata_1_p,
                        unsigned indatabsz,
                        void *__restrict outdata_p,
                        unsigned outdatabsz)
{
    unsigned i = indatabsz;
    if ((outdatabsz * 2) < i)
        i = (outdatabsz * 2);

    const float* indata_0 = (const float*)indata_0_p;
    const float* indata_1 = (const float*)indata_1_p;
    int16_t* outdata = (int16_t*)outdata_p;
    const float scale = 1.0f / CONV_SCALE;

#include "conv_i16_f32_i16_neon.inc"

    for(; i >= 64; i -= 64)
    {
        uint64x2_t cf0 = vld1q_u64((uint64_t*)(indata_0 + 0));
        uint64x2_t cf1 = vld1q_u64((uint64_t*)(indata_0 + 4));
        uint64x2_t cf2 = vld1q_u64((uint64_t*)(indata_1 + 0));
        uint64x2_t cf3 = vld1q_u64((uint64_t*)(indata_1 + 4));

        float32x4_t icf0 = vreinterpretq_f32_u64(vcombine_u64(vget_low_u64(cf0), vget_low_u64(cf2)));
        float32x4_t icf1 = vreinterpretq_f32_u64(vcombine_u64(vget_high_u64(cf0), vget_high_u64(cf2)));
        float32x4_t icf2 = vreinterpretq_f32_u64(vcombine_u64(vget_low_u64(cf1), vget_low_u64(cf3)));
        float32x4_t icf3 = vreinterpretq_f32_u64(vcombine_u64(vget_high_u64(cf1), vget_high_u64(cf3)));

        CONV_F32_I16(icf0, icf1, outdata + 0);
        CONV_F32_I16(icf2, icf3, outdata + 8);
        indata_0 += 8;
        indata_1 += 8;
        outdata += 16;
    }

    for (; i >= 16; i -= 16, indata_0 += 2, indata_1 += 2, outdata += 4) {
        int16_t a = indata_0[0] / CONV_SCALE;
        int16_t b = indata_0[1] / CONV_SCALE;
        int16_t c = indata_1[0] / CONV_SCALE;
        int16_t d = indata_1[1] / CONV_SCALE;

        uint64_t v = (uint64_t)(uint16_t)a | ((uint64_t)(uint16_t)b << 16) | ((uint64_t)(uint16_t)c << 32) | ((uint64_t)(uint16_t)d << 48);
        *(uint64_t*)outdata = v;
    }

    // do nothing with leftover
}

#undef TEMPLATE_FUNC_NAME
