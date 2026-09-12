for (; i >= 16; i -= 16)
{
    const int16_t i0 = *indata_0++;
    const int16_t q0 = *indata_0++;
    const int16_t i1 = *indata_1++;
    const int16_t q1 = *indata_1++;
    const int16_t i2 = *indata_2++;
    const int16_t q2 = *indata_2++;
    const int16_t i3 = *indata_3++;
    const int16_t q3 = *indata_3++;

    wu_i16u32_t a0 = {{i0, q0}};
    wu_i16u32_t a1 = {{i1, q1}};
    wu_i16u32_t a2 = {{i2, q2}};
    wu_i16u32_t a3 = {{i3, q3}};

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
