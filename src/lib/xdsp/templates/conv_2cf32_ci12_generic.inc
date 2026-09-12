for (; i >= 16; i -= 16) {

    float f0 = *(indata_0++) / CONV_SCALE;
    float f1 = *(indata_0++) / CONV_SCALE;
    float f2 = *(indata_1++) / CONV_SCALE;
    float f3 = *(indata_1++) / CONV_SCALE;

    wu_i16u32_t a0 = {{I16RND(f0), I16RND(f1)}};
    wu_i16u32_t a1 = {{I16RND(f2), I16RND(f3)}};

    wu_u32b_t  c0 = {(a0.u & 0xfff00000) | ((a0.u << 4) & 0x000fff00)};
    wu_u32b_t  c1 = {(a1.u & 0xfff00000) | ((a1.u << 4) & 0x000fff00)};

    *(outdata++) = c0.b[1];
    *(outdata++) = c0.b[2];
    *(outdata++) = c0.b[3];

    *(outdata++) = c1.b[1];
    *(outdata++) = c1.b[2];
    *(outdata++) = c1.b[3];
}
