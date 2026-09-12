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
