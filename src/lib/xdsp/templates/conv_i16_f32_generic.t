static
void TEMPLATE_FUNC_NAME(const void *__restrict indata,
                        unsigned indatabsz,
                        void *__restrict p_outdata,
                        unsigned outdatabsz)
{
    unsigned i = indatabsz;
    if ((outdatabsz / 2) < i)
        i = (outdatabsz / 2);

    const uint64_t *ld = (const uint64_t *)indata;

    float* outdata = (float*)p_outdata;

    for (; i >= 8; i -= 8) {
        uint64_t v = *(ld++);
        float a = (int16_t)(v);
        float b = (int16_t)(v>>16);
        float c = (int16_t)(v>>32);
        float d = (int16_t)(v>>48);

        *(outdata++) = a * CONV_SCALE;
        *(outdata++) = b * CONV_SCALE;
        *(outdata++) = c * CONV_SCALE;
        *(outdata++) = d * CONV_SCALE;
    }

    const int16_t *ldw = (const int16_t *)ld;
    for (; i >= 2; i -= 2) {
        *(outdata++) = *(ldw++) * CONV_SCALE;
    }
}

#undef TEMPLATE_FUNC_NAME
