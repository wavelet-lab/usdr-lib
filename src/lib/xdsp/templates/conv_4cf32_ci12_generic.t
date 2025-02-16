static
void TEMPLATE_FUNC_NAME(const void *__restrict indata_0_p,
                        const void *__restrict indata_1_p,
                        const void *__restrict indata_2_p,
                        const void *__restrict indata_3_p,
                        unsigned indatabsz,
                        void *__restrict outdata_p,
                        unsigned outdatabsz)
{
    unsigned i = indatabsz;
    if ((outdatabsz * 8 / 3) < i)
        i = (outdatabsz * 8 / 3);

    const float* indata_0 = (const float*)indata_0_p;
    const float* indata_1 = (const float*)indata_1_p;
    const float* indata_2 = (const float*)indata_2_p;
    const float* indata_3 = (const float*)indata_3_p;

    uint8_t* outdata = (uint8_t*)outdata_p;

    for (; i >= 32; i -= 32) {

        float f0 = *(indata_0++) / CONV_SCALE;
        float f1 = *(indata_0++) / CONV_SCALE;
        float f2 = *(indata_1++) / CONV_SCALE;
        float f3 = *(indata_1++) / CONV_SCALE;
        float f4 = *(indata_2++) / CONV_SCALE;
        float f5 = *(indata_2++) / CONV_SCALE;
        float f6 = *(indata_3++) / CONV_SCALE;
        float f7 = *(indata_3++) / CONV_SCALE;

        wu_i16u32_t a0 = {{I16RND(f0), I16RND(f1)}};
        wu_i16u32_t a1 = {{I16RND(f2), I16RND(f3)}};
        wu_i16u32_t a2 = {{I16RND(f4), I16RND(f5)}};
        wu_i16u32_t a3 = {{I16RND(f6), I16RND(f7)}};

        wu_u32b_t  c0 = {(a0.u & 0xfff00000) | ((a0.u << 4) & 0x000fff00)};
        wu_u32b_t  c1 = {(a1.u & 0xfff00000) | ((a1.u << 4) & 0x000fff00)};
        wu_u32b_t  c2 = {(a2.u & 0xfff00000) | ((a2.u << 4) & 0x000fff00)};
        wu_u32b_t  c3 = {(a3.u & 0xfff00000) | ((a3.u << 4) & 0x000fff00)};

        const wu_u32b_t arr[] = {c0, c1, c2, c3};
        for(unsigned j = 0; j < 4; ++j)
        {
            *(outdata++) = arr[j].b[1];
            *(outdata++) = arr[j].b[2];
            *(outdata++) = arr[j].b[3];
        }
    }
}
#undef TEMPLATE_FUNC_NAME
