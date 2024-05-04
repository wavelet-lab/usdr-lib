static
void TEMPLATE_FUNC_NAME(const int16_t *__restrict data,
                        const int16_t *__restrict conv,
                        int16_t *__restrict out,
                        unsigned count,
                        unsigned decim_bits,
                        unsigned flen)
{
    unsigned i, n;
    for (n = 0; n < count; n += (1 << decim_bits)) {
        int32_t acc = 0;
        for (i = 0; i < flen; i++) {
            acc += (int32_t)data[n + i] * (int32_t)conv[i];
        }
        out[(n >> decim_bits) + 0] = acc >> 15;
    }
}

#undef TEMPLATE_FUNC_NAME
