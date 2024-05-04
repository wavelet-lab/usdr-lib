static
void TEMPLATE_FUNC_NAME(const int16_t *__restrict data,
                        const int16_t *__restrict conv,
                        int16_t *__restrict out,
                        unsigned count,
                        unsigned interp,
                        unsigned flen)
{
    const unsigned shift = (interp == 1) ? 15 :
                               (interp == 2) ? 14 :
                               (interp == 4) ? 13 :
                               (interp == 8) ? 12 :
                               (interp ==16) ? 11 : 10;
    unsigned i, n, z;
    for (n = 0; n < count; n += 2) {
        for (z = 0; z < interp; z++) {
            int32_t acc[2] = {0, 0};
            for (i = 0; i < flen; i++) {
                acc[0] += (int32_t)data[n + 2 * i + 0] * (int32_t)conv[i + z * flen];
                acc[1] += (int32_t)data[n + 2 * i + 1] * (int32_t)conv[i + z * flen];
            }
            out[interp * n + 2 * z + 0] = acc[0] >> shift;
            out[interp * n + 2 * z + 1] = acc[1] >> shift;
        }
    }
}

#undef TEMPLATE_FUNC_NAME
