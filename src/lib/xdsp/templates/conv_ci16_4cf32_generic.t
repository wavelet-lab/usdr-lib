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
