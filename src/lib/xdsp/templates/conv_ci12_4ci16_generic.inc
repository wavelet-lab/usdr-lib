for (; i >= 12; i -= 12) {
    /* read 12 bytes -> 4ci16 */

    uint64_t v0 = *(const uint64_t *)(indata + 0);
    uint64_t v1 = *(const uint64_t *)(indata + 6);
    indata += 12;

    *(outdata_0++) = (int16_t)((v0 <<  4)         );
    *(outdata_0++) = (int16_t)((v0 >>  8) & 0xfff0);
    *(outdata_1++) = (int16_t)((v0 >> 20) & 0xfff0);
    *(outdata_1++) = (int16_t)((v0 >> 32) & 0xfff0);
    *(outdata_2++) = (int16_t)((v1 <<  4)         );
    *(outdata_2++) = (int16_t)((v1 >>  8) & 0xfff0);
    *(outdata_3++) = (int16_t)((v1 >> 20) & 0xfff0);
    *(outdata_3++) = (int16_t)((v1 >> 32) & 0xfff0);
}
