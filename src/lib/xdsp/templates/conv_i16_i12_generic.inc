for (; i >= 4; i -= 4) {

    const int16_t b0 = *indata++;
    const int16_t b1 = *indata++;

    wu_i16u32_t a = {{b0, b1}};
    wu_u32b_t   c = {(a.u & 0xfff00000) | ((a.u << 4) & 0x000fff00)};

    *(outdata++) = c.b[1];
    *(outdata++) = c.b[2];
    *(outdata++) = c.b[3];
}

if(i >= 2)
{
    wu_i16b_t c = {*indata};

    *(outdata++) = c.b[0];
    *(outdata++) = c.b[1] >> 4;
    i -= 2;
}
