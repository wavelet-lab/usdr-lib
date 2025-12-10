static
void TEMPLATE_FUNC_NAME(const void *__restrict indata,
                        unsigned indatabsz,
                        void *__restrict outdata_0_p,
                        void *__restrict outdata_1_p,
                        void *__restrict outdata_2_p,
                        unsigned outdatabsz)
{
    unsigned i = indatabsz;
    if ((outdatabsz / 2) < i)
        i = (outdatabsz / 2);

    const uint32_t *ld = (const uint32_t *)indata;

    float* outdata_0 = (float*)outdata_0_p;
    float* outdata_1 = (float*)outdata_1_p;
    float* outdata_2 = (float*)outdata_2_p;

    for (; i >= 12; i -= 12, ld += 3) {
        uint64_t v = *((uint64_t*)ld);
        uint32_t w = *(ld + 2);
        float a = (int16_t)(v);
        float b = (int16_t)(v>>16);
        float c = (int16_t)(v>>32);
        float d = (int16_t)(v>>48);
        float e = (int16_t)(w);
        float f = (int16_t)(w>>16);

        *(outdata_0++) = a * CONV_SCALE;
        *(outdata_0++) = b * CONV_SCALE;
        *(outdata_1++) = c * CONV_SCALE;
        *(outdata_1++) = d * CONV_SCALE;
        *(outdata_2++) = e * CONV_SCALE;
        *(outdata_2++) = f * CONV_SCALE;
    }

    // do nothing with leftover
}

#undef TEMPLATE_FUNC_NAME
