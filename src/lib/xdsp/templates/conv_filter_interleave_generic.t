static
void TEMPLATE_FUNC_NAME(const int16_t *__restrict data,
                        const int16_t *__restrict conv,
                        int16_t *__restrict out,
                        unsigned count,
                        unsigned decim_bits,
                        unsigned flen)
{
    unsigned i, n;
    for (n = 0; n < count; n += (2 << decim_bits)) {
        int32_t acc[2] = {0, 0};
        for (i = 0; i < flen; i++) {
            acc[0] += (int32_t)data[n + 2 * i + 0] * (int32_t)conv[i];
            acc[1] += (int32_t)data[n + 2 * i + 1] * (int32_t)conv[i];
        }
        out[(n >> decim_bits) + 0] = acc[0] >> 15;
        out[(n >> decim_bits) + 1] = acc[1] >> 15;
    }
}

#undef TEMPLATE_FUNC_NAME
