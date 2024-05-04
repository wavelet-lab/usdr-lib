static
void TEMPLATE_FUNC_NAME(const int16_t *__restrict data,
                        const int16_t *__restrict conv,
                        int16_t *__restrict out,
                        unsigned count,
                        unsigned interp,
                        unsigned flen)
{
    unsigned i, n, z;
    const unsigned shift = (interp == 1) ? 15 :
                               (interp == 2) ? 14 :
                               (interp == 4) ? 13 :
                               (interp == 8) ? 12 :
                               (interp ==16) ? 11 : 10;

    for (n = 0; n < count; n++) {
        for (z = 0; z < interp; z++) {
            int32_t acc = 0;
            for (i = 0; i < flen; i++) {
                acc += (int32_t)data[n + i] * (int32_t)conv[i + z * flen];
            }
            out[interp * n + z] = acc >> shift;
        }
    }
}

#undef TEMPLATE_FUNC_NAME
