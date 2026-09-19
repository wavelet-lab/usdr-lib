for (; i >= 8; i -= 8) {

    float f0 = *(indata++) / CONV_SCALE;
    float f1 = *(indata++) / CONV_SCALE;

    wu_i16u32_t a = {{I16RND(f0), I16RND(f1)}};
    wu_u32b_t   c = {(a.u & 0xfff00000) | ((a.u << 4) & 0x000fff00)};

    *(outdata++) = c.b[1];
    *(outdata++) = c.b[2];
    *(outdata++) = c.b[3];
}

if(i >= 4)
{
    float f = *indata / CONV_SCALE;
    wu_i16b_t c = {I16RND(f)};

    *(outdata++) = c.b[0];
    *(outdata++) = c.b[1] >> 4;
    i -= 4;
}
