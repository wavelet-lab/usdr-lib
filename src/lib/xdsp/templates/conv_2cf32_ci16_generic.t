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
