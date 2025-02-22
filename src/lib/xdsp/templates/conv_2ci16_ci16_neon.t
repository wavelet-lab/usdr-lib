static
void TEMPLATE_FUNC_NAME(const void *__restrict indata_0_p,
                        const void *__restrict indata_1_p,
                        unsigned indatabsz,
                        void *__restrict outdata_p,
                        unsigned outdatabsz)
{
    unsigned i = indatabsz;
    if ((outdatabsz) < i)
        i = (outdatabsz);

    const int16_t* indata_0 = (const int16_t*)indata_0_p;
    const int16_t* indata_1 = (const int16_t*)indata_1_p;
    int16_t* outdata = (int16_t*)outdata_p;

    while(i >= 32)
    {
        vst2q_u32((uint32_t*)outdata,
            (uint32x4x2_t){ {vld1q_u32((uint32_t*)indata_0), vld1q_u32((uint32_t*)indata_1)} });

        i -= 32;
        indata_0 += 8;
        indata_1 += 8;
        outdata += 16;
    }

    for (; i >= 8; i -= 8, indata_0 += 2, indata_1 += 2, outdata += 4) {
        int16_t a = indata_0[0];
        int16_t b = indata_0[1];
        int16_t c = indata_1[0];
        int16_t d = indata_1[1];

        uint64_t v = (uint64_t)(uint16_t)a | ((uint64_t)(uint16_t)b << 16) | ((uint64_t)(uint16_t)c << 32) | ((uint64_t)(uint16_t)d << 48);
        *(uint64_t*)outdata = v;
    }

    // do nothing with leftover
}

#undef TEMPLATE_FUNC_NAME
