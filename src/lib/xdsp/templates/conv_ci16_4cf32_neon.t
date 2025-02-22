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
    if ((outdatabsz / 2) < i)
        i = (outdatabsz / 2);

    float* outdata_0 = (float*)outdata_0_p;
    float* outdata_1 = (float*)outdata_1_p;
    float* outdata_2 = (float*)outdata_2_p;
    float* outdata_3 = (float*)outdata_3_p;
    const int16_t *indata = (const int16_t*)indata_p;

#include "conv_i16_f32_i16_neon.inc"

    while(i >= 32)
    {
        uint64x2_t cf0 = vreinterpretq_u64_f32(CONV_I16_F32(indata +  0));
        uint64x2_t cf1 = vreinterpretq_u64_f32(CONV_I16_F32(indata +  4));
        uint64x2_t cf2 = vreinterpretq_u64_f32(CONV_I16_F32(indata +  8));
        uint64x2_t cf3 = vreinterpretq_u64_f32(CONV_I16_F32(indata + 12));

        vst1q_u64((uint64_t*)(outdata_0), vcombine_u64(vget_low_u64(cf0), vget_low_u64(cf2)));
        vst1q_u64((uint64_t*)(outdata_1), vcombine_u64(vget_high_u64(cf0), vget_high_u64(cf2)));
        vst1q_u64((uint64_t*)(outdata_2), vcombine_u64(vget_low_u64(cf1), vget_low_u64(cf3)));
        vst1q_u64((uint64_t*)(outdata_3), vcombine_u64(vget_high_u64(cf1), vget_high_u64(cf3)));

        outdata_0 += 4;
        outdata_1 += 4;
        outdata_2 += 4;
        outdata_3 += 4;
        indata += 16;
        i -= 32;
    }

    const uint64_t *ld = (const uint64_t *)indata;

    for (; i >= 16; i -= 16)
    {
        const uint64_t v0 = *(ld++);
        const uint64_t v1 = *(ld++);

        const float i0 = (int16_t)(v0);
        const float q0 = (int16_t)(v0>>16);
        const float i1 = (int16_t)(v0>>32);
        const float q1 = (int16_t)(v0>>48);
        const float i2 = (int16_t)(v1);
        const float q2 = (int16_t)(v1>>16);
        const float i3 = (int16_t)(v1>>32);
        const float q3 = (int16_t)(v1>>48);

        *(outdata_0++) = i0 * CONV_SCALE;
        *(outdata_0++) = q0 * CONV_SCALE;
        *(outdata_1++) = i1 * CONV_SCALE;
        *(outdata_1++) = q1 * CONV_SCALE;
        *(outdata_2++) = i2 * CONV_SCALE;
        *(outdata_2++) = q2 * CONV_SCALE;
        *(outdata_3++) = i3 * CONV_SCALE;
        *(outdata_3++) = q3 * CONV_SCALE;
    }

    // do nothing with leftover
}

#undef TEMPLATE_FUNC_NAME
