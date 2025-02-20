static
void TEMPLATE_FUNC_NAME(const void *__restrict indata_p,
                        unsigned indatabsz,
                        void *__restrict outdata_0_p,
                        void *__restrict outdata_1_p,
                        unsigned outdatabsz)
{
    unsigned i = indatabsz;
    if ((outdatabsz / 2) < i)
        i = (outdatabsz / 2);

    float* outdata_0 = (float*)outdata_0_p;
    float* outdata_1 = (float*)outdata_1_p;
    const int16_t *indata = (const int16_t*)indata_p;

#include "conv_i16_f32_i16_neon.inc"

    while(i >= 32)
    {
        uint64x2_t cf0 = vreinterpretq_u64_f32(CONV_I16_F32(indata +  0));
        uint64x2_t cf1 = vreinterpretq_u64_f32(CONV_I16_F32(indata +  4));
        uint64x2_t cf2 = vreinterpretq_u64_f32(CONV_I16_F32(indata +  8));
        uint64x2_t cf3 = vreinterpretq_u64_f32(CONV_I16_F32(indata + 12));

        vst1q_u64((uint64_t*)(outdata_0 + 0), vcombine_u64(vget_low_u64(cf0), vget_low_u64(cf1)));
        vst1q_u64((uint64_t*)(outdata_0 + 4), vcombine_u64(vget_low_u64(cf2), vget_low_u64(cf3)));
        vst1q_u64((uint64_t*)(outdata_1 + 0), vcombine_u64(vget_high_u64(cf0), vget_high_u64(cf1)));
        vst1q_u64((uint64_t*)(outdata_1 + 4), vcombine_u64(vget_high_u64(cf2), vget_high_u64(cf3)));

        outdata_0 += 8;
        outdata_1 += 8;
        indata += 16;
        i -= 32;
    }

    const uint64_t *ld = (const uint64_t *)indata;

    for (; i >= 8; i -= 8) {
        uint64_t v = *(ld++);
        float a = (int16_t)(v);
        float b = (int16_t)(v>>16);
        float c = (int16_t)(v>>32);
        float d = (int16_t)(v>>48);

        *(outdata_0++) = a * CONV_SCALE;
        *(outdata_0++) = b * CONV_SCALE;
        *(outdata_1++) = c * CONV_SCALE;
        *(outdata_1++) = d * CONV_SCALE;
    }

    // do nothing with leftover
}

#undef TEMPLATE_FUNC_NAME
