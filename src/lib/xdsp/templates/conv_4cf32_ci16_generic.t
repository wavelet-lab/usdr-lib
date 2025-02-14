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
    if ((outdatabsz * 2) < i)
        i = (outdatabsz * 2);

    const float* indata_0 = (const float*)indata_0_p;
    const float* indata_1 = (const float*)indata_1_p;
    const float* indata_2 = (const float*)indata_2_p;
    const float* indata_3 = (const float*)indata_3_p;
    uint64_t* outdata = (uint64_t*)outdata_p;

    for (; i >= 32; i -= 32)
    {
        const int16_t i0 = *(indata_0++) / CONV_SCALE;
        const int16_t q0 = *(indata_0++) / CONV_SCALE;
        const int16_t i1 = *(indata_1++) / CONV_SCALE;
        const int16_t q1 = *(indata_1++) / CONV_SCALE;
        const int16_t i2 = *(indata_2++) / CONV_SCALE;
        const int16_t q2 = *(indata_2++) / CONV_SCALE;
        const int16_t i3 = *(indata_3++) / CONV_SCALE;
        const int16_t q3 = *(indata_3++) / CONV_SCALE;

        *outdata++ = (uint64_t)(uint16_t)i0 | ((uint64_t)(uint16_t)q0 << 16) | ((uint64_t)(uint16_t)i1 << 32) | ((uint64_t)(uint16_t)q1 << 48);
        *outdata++ = (uint64_t)(uint16_t)i2 | ((uint64_t)(uint16_t)q2 << 16) | ((uint64_t)(uint16_t)i3 << 32) | ((uint64_t)(uint16_t)q3 << 48);
    }

    // do nothing with leftover
}

#undef TEMPLATE_FUNC_NAME
