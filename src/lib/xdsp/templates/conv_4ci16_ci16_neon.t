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
    if ((outdatabsz) < i)
        i = (outdatabsz);

    const uint32_t* indata_0 = (uint32_t*)indata_0_p;
    const uint32_t* indata_1 = (uint32_t*)indata_1_p;
    const uint32_t* indata_2 = (uint32_t*)indata_2_p;
    const uint32_t* indata_3 = (uint32_t*)indata_3_p;

    uint64_t* outdata = (uint64_t*)outdata_p;

    while(i >= 64)
    {
        vst4q_u32((uint32_t*)outdata,
            (uint32x4x4_t)({ {vld1q_u32(indata_0), vld1q_u32(indata_1), vld1q_u32(indata_2), vld1q_u32(indata_3)} });

        i -= 64;
        indata_0 += 4;
        indata_1 += 4;
        indata_2 += 4;
        indata_3 += 4;
        outdata += 8;
    }

    for (; i >= 16; i -= 16)
    {
        const uint32_t iq0 = *indata_0++;
        const uint32_t iq1 = *indata_1++;
        const uint32_t iq2 = *indata_2++;
        const uint32_t iq3 = *indata_3++;

        *(uint64_t*)outdata++ = (uint64_t)iq0 | ((uint64_t)iq1 << 32);
        *(uint64_t*)outdata++ = (uint64_t)iq2 | ((uint64_t)iq3 << 32);
    }

    // do nothing with leftover
}

#undef TEMPLATE_FUNC_NAME
