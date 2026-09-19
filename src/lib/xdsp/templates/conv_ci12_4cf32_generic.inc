for (; i >= 12; i -= 12) {
    /* read 12 bytes -> 2*48 bits -> 4*2 floats -> 4cf32 */

    uint64_t v0 = *(const uint64_t *)(indata + 0);
    uint64_t v1 = ((uint64_t)((*(const uint32_t *)(indata + 8))) << 16) | (v0 >> 48);
    indata += 12;

    float i0 = (int16_t)(v0 << 4);
    float q0 = (int16_t)((v0 >> 8) & 0xfff0);
    float i1 = (int16_t)((v0 >> 20) & 0xfff0);
    float q1 = (int16_t)((v0 >> 32)  & 0xfff0);
    float i2 = (int16_t)(v1 << 4);
    float q2 = (int16_t)((v1 >> 8) & 0xfff0);
    float i3 = (int16_t)((v1 >> 20) & 0xfff0);
    float q3 = (int16_t)((v1 >> 32)  & 0xfff0);

    *(outdata_0++) = i0 * CONV_SCALE;
    *(outdata_0++) = q0 * CONV_SCALE;
    *(outdata_1++) = i1 * CONV_SCALE;
    *(outdata_1++) = q1 * CONV_SCALE;
    *(outdata_2++) = i2 * CONV_SCALE;
    *(outdata_2++) = q2 * CONV_SCALE;
    *(outdata_3++) = i3 * CONV_SCALE;
    *(outdata_3++) = q3 * CONV_SCALE;
}

// tail ignored
