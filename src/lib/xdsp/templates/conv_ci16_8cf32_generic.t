static
void TEMPLATE_FUNC_NAME(const void *__restrict indata,
                        unsigned indatabsz,
                        void *__restrict outdata_0_p,
                        void *__restrict outdata_1_p,
                        void *__restrict outdata_2_p,
                        void *__restrict outdata_3_p,
                        void *__restrict outdata_4_p,
                        void *__restrict outdata_5_p,
                        void *__restrict outdata_6_p,
                        void *__restrict outdata_7_p,
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
    float* outdata_4 = (float*)outdata_4_p;
    float* outdata_5 = (float*)outdata_5_p;
    float* outdata_6 = (float*)outdata_6_p;
    float* outdata_7 = (float*)outdata_7_p;

    for (; i >= 32; i -= 32)
    {
        const uint64_t v0 = *(ld++);
        const uint64_t v1 = *(ld++);
        const uint64_t v2 = *(ld++);
        const uint64_t v3 = *(ld++);

        const float i0 = (int16_t)(v0);
        const float q0 = (int16_t)(v0>>16);
        const float i1 = (int16_t)(v0>>32);
        const float q1 = (int16_t)(v0>>48);
        const float i2 = (int16_t)(v1);
        const float q2 = (int16_t)(v1>>16);
        const float i3 = (int16_t)(v1>>32);
        const float q3 = (int16_t)(v1>>48);
        const float i4 = (int16_t)(v2);
        const float q4 = (int16_t)(v2>>16);
        const float i5 = (int16_t)(v2>>32);
        const float q5 = (int16_t)(v2>>48);
        const float i6 = (int16_t)(v3);
        const float q6 = (int16_t)(v3>>16);
        const float i7 = (int16_t)(v3>>32);
        const float q7 = (int16_t)(v3>>48);

        *(outdata_0++) = i0 * CONV_SCALE;
        *(outdata_0++) = q0 * CONV_SCALE;
        *(outdata_1++) = i1 * CONV_SCALE;
        *(outdata_1++) = q1 * CONV_SCALE;
        *(outdata_2++) = i2 * CONV_SCALE;
        *(outdata_2++) = q2 * CONV_SCALE;
        *(outdata_3++) = i3 * CONV_SCALE;
        *(outdata_3++) = q3 * CONV_SCALE;

        *(outdata_4++) = i4 * CONV_SCALE;
        *(outdata_4++) = q4 * CONV_SCALE;
        *(outdata_5++) = i5 * CONV_SCALE;
        *(outdata_5++) = q5 * CONV_SCALE;
        *(outdata_6++) = i6 * CONV_SCALE;
        *(outdata_6++) = q6 * CONV_SCALE;
        *(outdata_7++) = i7 * CONV_SCALE;
        *(outdata_7++) = q7 * CONV_SCALE;
    }

    // do nothing with leftover
}

#undef TEMPLATE_FUNC_NAME
