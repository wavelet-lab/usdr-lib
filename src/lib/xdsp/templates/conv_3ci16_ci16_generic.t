
static
void TEMPLATE_FUNC_NAME(const void *__restrict indata_0_p,
                        const void *__restrict indata_1_p,
                        const void *__restrict indata_2_p,
                        unsigned indatabsz,
                        void *__restrict outdata_p,
                        unsigned outdatabsz)
{
    unsigned i = indatabsz;
    if ((outdatabsz) < i)
        i = (outdatabsz);

    const int16_t* indata_0 = (int16_t*)indata_0_p;
    const int16_t* indata_1 = (int16_t*)indata_1_p;
    const int16_t* indata_2 = (int16_t*)indata_2_p;
    int16_t* outdata = (int16_t*)outdata_p;

    for (; i >= 12; i -= 12, indata_0 += 2, indata_1 += 2, indata_2 += 2, outdata += 6) {
        int16_t a = indata_0[0];
        int16_t b = indata_0[1];
        int16_t c = indata_1[0];
        int16_t d = indata_1[1];
        int16_t e = indata_2[0];
        int16_t f = indata_2[1];

        uint64_t v = (uint64_t)(uint16_t)a | ((uint64_t)(uint16_t)b << 16) | ((uint64_t)(uint16_t)c << 32) | ((uint64_t)(uint16_t)d << 48);
        uint32_t w = (uint32_t)(uint16_t)e | ((uint32_t)(uint16_t)f << 16);
        *(uint64_t*)outdata = v;
        *(uint32_t*)(outdata + 4) = w;
    }

    // do nothing with leftover
}

#undef TEMPLATE_FUNC_NAME
