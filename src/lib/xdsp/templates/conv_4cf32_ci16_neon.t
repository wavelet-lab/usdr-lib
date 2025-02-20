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
    if ((outdatabsz * 2) < i)
        i = (outdatabsz * 2);

    const float* indata_0 = (const float*)indata_0_p;
    const float* indata_1 = (const float*)indata_1_p;
    const float* indata_2 = (const float*)indata_2_p;
    const float* indata_3 = (const float*)indata_3_p;
    uint64_t* outdata = (uint64_t*)outdata_p;
    const float scale = 1.0f / CONV_SCALE;

#include "conv_i16_f32_i16_neon.inc"

    for(; i >= 64; i -= 64)
    {
        uint64x2_t cf0 = vld1q_u64((uint64_t*)(indata_0));
        uint64x2_t cf1 = vld1q_u64((uint64_t*)(indata_1));
        uint64x2_t cf2 = vld1q_u64((uint64_t*)(indata_2));
        uint64x2_t cf3 = vld1q_u64((uint64_t*)(indata_3));

        float32x4_t icf0 = vreinterpretq_f32_u64(vcombine_u64(vget_low_u64(cf0), vget_low_u64(cf1)));
        float32x4_t icf1 = vreinterpretq_f32_u64(vcombine_u64(vget_low_u64(cf2), vget_low_u64(cf3)));
        float32x4_t icf2 = vreinterpretq_f32_u64(vcombine_u64(vget_high_u64(cf0), vget_high_u64(cf1)));
        float32x4_t icf3 = vreinterpretq_f32_u64(vcombine_u64(vget_high_u64(cf2), vget_high_u64(cf3)));

        CONV_F32_I16(icf0, icf1, outdata + 0);
        CONV_F32_I16(icf2, icf3, outdata + 8);
        indata_0 += 4;
        indata_1 += 4;
        indata_2 += 4;
        indata_3 += 4;
        outdata += 16;
    }

    for (; i >= 32; i -= 32)
    {
        const int16_t i0 = *(indata_0++) / CONV_SCALE;
        const int16_t q0 = *(indata_0++) / CONV_SCALE;
        const int16_t i1 = *(indata_1++) / CONV_SCALE;
        const int16_t q1 = *(indata_1++) / CONV_SCALE;
        const int16_t i2 = *(indata_2++) / CONV_SCALE;
        const int16_t q2 = *(indata_2++) / CONV_SCALE;
        const int16_t i3 = *(indata_3++) / CONV_SCALE;
        const int16_t q3 = *(indata_3++) / CONV_SCALE;

        *outdata++ = (uint64_t)(uint16_t)i0 | ((uint64_t)(uint16_t)q0 << 16) | ((uint64_t)(uint16_t)i1 << 32) | ((uint64_t)(uint16_t)q1 << 48);
        *outdata++ = (uint64_t)(uint16_t)i2 | ((uint64_t)(uint16_t)q2 << 16) | ((uint64_t)(uint16_t)i3 << 32) | ((uint64_t)(uint16_t)q3 << 48);
    }

    // do nothing with leftover
}

#undef TEMPLATE_FUNC_NAME
