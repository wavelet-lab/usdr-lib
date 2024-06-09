static
void TEMPLATE_FUNC_NAME(const void *__restrict indata_p,
                        unsigned indatabsz,
                        void *__restrict outdata_p,
                        unsigned outdatabsz)
{
    unsigned i = indatabsz;
    if ((outdatabsz * 8 / 3) < i)
        i = (outdatabsz * 8 / 3);

    const float* indata = (const float*)indata_p;
    uint8_t* outdata = (uint8_t*)outdata_p;

    union u32b {uint32_t i; uint8_t b[4];};
    typedef union u32b u32b_t;

    union i16b {int16_t i; uint8_t b[2];};
    typedef union i16b i16b_t;

    union i16u32 {int16_t i[2]; uint32_t u;};
    typedef union i16u32 i16u32_t;

    for (; i >= 8; i -= 8) {

        float f0 = *(indata++) / CONV_SCALE;
        float f1 = *(indata++) / CONV_SCALE;

        i16u32_t a = {I16RND(f0), I16RND(f1)};
        u32b_t   c = {(a.u & 0xfff00000) | ((a.u << 4) & 0x000fff00)};

        *(outdata++) = c.b[1];
        *(outdata++) = c.b[2];
        *(outdata++) = c.b[3];
    }

    if(i >= 4)
    {
        float f = *indata / CONV_SCALE;
        i16b_t c = {I16RND(f)};

        *(outdata++) = c.b[0];
        *(outdata++) = c.b[1] >> 4;
        i -= 4;
    }
}

#undef TEMPLATE_FUNC_NAME
