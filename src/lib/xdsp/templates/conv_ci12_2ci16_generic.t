static
void TEMPLATE_FUNC_NAME(const void *__restrict indata_p,
                        unsigned indatabsz,
                        void *__restrict outdata_0_p,
                        void *__restrict outdata_1_p,
                        unsigned outdatabsz)
{
    unsigned i = indatabsz;
    /* 12 bits -> 16 bits  =>  3 -> 4   */
    if ((outdatabsz * 3 / 4) < i)
        i = (outdatabsz * 3 / 4);

    const uint8_t* indata = (const uint8_t*)indata_p;
    int16_t* outdata_0 = (int16_t*)outdata_0_p;
    int16_t* outdata_1 = (int16_t*)outdata_1_p;

    for (; i >= 6; i -= 6)
    {
        /* read 48 bits -> 4 int16 (64 bits) */

        uint64_t v = *(const uint64_t *)indata;
        indata += 6;

        *(outdata_0++) = (int16_t)((v <<  4)         );
        *(outdata_0++) = (int16_t)((v >>  8) & 0xfff0);
        *(outdata_1++) = (int16_t)((v >> 20) & 0xfff0);
        *(outdata_1++) = (int16_t)((v >> 32) & 0xfff0);
    }
    // do nothing with tail
}

#undef TEMPLATE_FUNC_NAME
