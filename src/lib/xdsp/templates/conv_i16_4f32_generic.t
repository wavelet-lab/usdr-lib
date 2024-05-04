static
void TEMPLATE_FUNC_NAME(const void *__restrict indata,
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

    const uint64_t *ld = (const uint64_t *)indata;

    float* outdata_0 = (float*)outdata_0_p;
    float* outdata_1 = (float*)outdata_1_p;
    float* outdata_2 = (float*)outdata_2_p;
    float* outdata_3 = (float*)outdata_3_p;

    for (; i >= 8; i -= 8) {
        uint64_t v = *(ld++);
        float a = (int16_t)(v);
        float b = (int16_t)(v>>16);
        float c = (int16_t)(v>>32);
        float d = (int16_t)(v>>48);

        *(outdata_0++) = a * CONV_SCALE;
        *(outdata_1++) = b * CONV_SCALE;
        *(outdata_2++) = c * CONV_SCALE;
        *(outdata_3++) = d * CONV_SCALE;
    }

    // do nothing with leftover
}

#undef TEMPLATE_FUNC_NAME
