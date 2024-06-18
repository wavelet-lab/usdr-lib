static
void TEMPLATE_FUNC_NAME(const void *__restrict indata_p,
                        unsigned indatabsz,
                        void *__restrict outdata_0_p,
                        void *__restrict outdata_1_p,
                        unsigned outdatabsz)
{
    unsigned i = indatabsz;
    /* 12 bits -> 32 bits  =>  3 -> 8   */
    if ((outdatabsz * 3 / 8) < i)
        i = (outdatabsz * 3 / 8);

    const uint8_t* indata = (const uint8_t*)indata_p;
    float* outdata_0 = (float*)outdata_0_p;
    float* outdata_1 = (float*)outdata_1_p;

    for (; i >= 6; i -= 6) {
        /* read 48 bits -> 4 floats */

        uint64_t v = *(const uint64_t *)indata;
        indata += 6;

        float a = (int16_t)(v << 4);
        float b = (int16_t)((v >> 8) & 0xfff0);
        float c = (int16_t)((v >> 20) & 0xfff0);
        float d = (int16_t)((v >> 32)  & 0xfff0);

        *(outdata_0++) = a * CONV_SCALE;
        *(outdata_0++) = b * CONV_SCALE;
        *(outdata_1++) = c * CONV_SCALE;
        *(outdata_1++) = d * CONV_SCALE;
    }

    float **dest = &outdata_0;

    while(i >= 3)
    {
        uint8_t v0 = *(indata++);
        uint8_t v1 = *(indata++);
        uint8_t v2 = *(indata++);
        i -= 3;

        float a = (int16_t) (((uint16_t)v0 << 4) | ((uint16_t)v1 << 12));
        float b = (int16_t) (((uint16_t)v2 << 8) | (v1 & 0xf0));

        *((*dest)++) = a * CONV_SCALE;
        *((*dest)++) = b * CONV_SCALE;

        dest = (*dest == outdata_0) ? &outdata_1 : &outdata_0;
    }

    if(i >= 2)
    {
        uint16_t v = *(const uint16_t*)indata;
        float a = (int16_t)(v << 4);
        *((*dest)++) = a * CONV_SCALE;
        i -= 2;
    }
}

#undef TEMPLATE_FUNC_NAME
