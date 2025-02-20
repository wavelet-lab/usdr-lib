static
void TEMPLATE_FUNC_NAME(const void *__restrict indata_p,
                        unsigned indatabsz,
                        void *__restrict outdata_0_p,
                        void *__restrict outdata_1_p,
                        unsigned outdatabsz)
{
    unsigned i = indatabsz;
    if ((outdatabsz) < i)
        i = (outdatabsz);

    const int16_t *indata = (const int16_t *)indata_p;
    int16_t* outdata_0 = (int16_t*)outdata_0_p;
    int16_t* outdata_1 = (int16_t*)outdata_1_p;

    while(i >= 32)
    {
        uint32x4x2_t reg = vld2q_u32((uint32_t*)indata);
        vst1q_u32((uint32_t*)outdata_0, reg.val[0]);
        vst1q_u32((uint32_t*)outdata_1, reg.val[1]);

        i -= 32;
        outdata_0 += 8;
        outdata_1 += 8;
        indata += 16;
    }

    const uint64_t *ld = (const uint64_t *)indata;

    for (; i >= 8; i -= 8) {
        uint64_t v = *(ld++);
        int16_t a = (int16_t)(v);
        int16_t b = (int16_t)(v>>16);
        int16_t c = (int16_t)(v>>32);
        int16_t d = (int16_t)(v>>48);

        *(outdata_0++) = a;
        *(outdata_0++) = b;
        *(outdata_1++) = c;
        *(outdata_1++) = d;
    }

    // do nothing with leftover
}

#undef TEMPLATE_FUNC_NAME
