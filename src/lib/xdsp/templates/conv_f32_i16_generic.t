static
void TEMPLATE_FUNC_NAME(const void *__restrict indata_p,
                        unsigned indatabsz,
                        void *__restrict outdata_p,
                        unsigned outdatabsz)
{
    unsigned i = indatabsz;
    if ((outdatabsz * 2) < i)
        i = (outdatabsz * 2);

    const float* indata = (const float*)indata_p;
    int16_t* outdata = (int16_t*)outdata_p;

    for (; i >= 16; i -= 16, indata += 4, outdata += 4) {
        int16_t a = indata[0] / CONV_SCALE;
        int16_t b = indata[1] / CONV_SCALE;
        int16_t c = indata[2] / CONV_SCALE;
        int16_t d = indata[3] / CONV_SCALE;

        uint64_t v = (uint64_t)(uint16_t)a | ((uint64_t)(uint16_t)b << 16) | ((uint64_t)(uint16_t)c << 32) | ((uint64_t)(uint16_t)d << 48);
        *(int64_t*)outdata = v;
    }

    for (; i >= 4; i -= 4) {
        *(outdata++) = *(indata++) * CONV_SCALE;
    }
}

#undef TEMPLATE_FUNC_NAME
