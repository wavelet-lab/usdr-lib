for (; i >= 8; i -= 8) {

    const int16_t i0 = *indata_0++;
    const int16_t q0 = *indata_0++;
    const int16_t i1 = *indata_1++;
    const int16_t q1 = *indata_1++;

    wu_i16u32_t a0 = {{i0, q0}};
    wu_i16u32_t a1 = {{i1, q1}};

    wu_u32b_t  c0 = {(a0.u & 0xfff00000) | ((a0.u << 4) & 0x000fff00)};
    wu_u32b_t  c1 = {(a1.u & 0xfff00000) | ((a1.u << 4) & 0x000fff00)};

    *(outdata++) = c0.b[1];
    *(outdata++) = c0.b[2];
    *(outdata++) = c0.b[3];

    *(outdata++) = c1.b[1];
    *(outdata++) = c1.b[2];
    *(outdata++) = c1.b[3];
}
