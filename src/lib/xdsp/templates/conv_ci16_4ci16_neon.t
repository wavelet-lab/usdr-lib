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
    if ((outdatabsz) < i)
        i = (outdatabsz);

    const uint32_t* indata = (uint32_t*)indata_p;

    uint32_t* outdata_0 = (uint32_t*)outdata_0_p;
    uint32_t* outdata_1 = (uint32_t*)outdata_1_p;
    uint32_t* outdata_2 = (uint32_t*)outdata_2_p;
    uint32_t* outdata_3 = (uint32_t*)outdata_3_p;

    while(i >= 64)
    {
        uint32x4x4_t reg = vld4q_u32(indata);
        vst1q_u32(outdata_0, reg.val[0]);
        vst1q_u32(outdata_1, reg.val[1]);
        vst1q_u32(outdata_2, reg.val[2]);
        vst1q_u32(outdata_3, reg.val[3]);

        i -= 64;
        outdata_0 += 4;
        outdata_1 += 4;
        outdata_2 += 4;
        outdata_3 += 4;
        indata += 16;
    }

    for (; i >= 16; i -= 16)
    {
        *outdata_0++ = *indata++;
        *outdata_1++ = *indata++;
        *outdata_2++ = *indata++;
        *outdata_3++ = *indata++;
    }

    // do nothing with leftover
}

#undef TEMPLATE_FUNC_NAME
