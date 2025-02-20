static
void TEMPLATE_FUNC_NAME(const void *__restrict indata_p,
                        unsigned indatabsz,
                        void *__restrict outdata_p,
                        unsigned outdatabsz)
{
    unsigned i = indatabsz;
    if ((outdatabsz * 4 / 3) < i)
        i = (outdatabsz * 4 / 3);

    const int16_t* indata = (const int16_t*)indata_p;
    uint8_t* outdata = (uint8_t*)outdata_p;

#include "conv_i16_i12_neon.inc"

#define CONVERT_I16_I12_BLOCK2(rlow, rmid, rhigh) \
    { \
        int16x8_t i0 = vld1q_s16(indata + 0); \
        int16x8_t i1 = vld1q_s16(indata + 8); \
        indata += 16;
        CONVERT_I16_I12_BLOCK(i0, i1, rlow, rmid, rhigh); \
    }
// CONVERT_I16_I12_BLOCK2 end

    uint8x8_t lo0, hi0, lo1, hi1, lo2, hi2;

    if(i >= 64)
    {
        CONVERT_I16_I12_BLOCK2(lo0, hi0, lo1);
        CONVERT_I16_I12_BLOCK2(hi1, lo2, hi2);

        for(; i >= 2*64; i -= 64)
        {
            vst1q_u8(outdata +  0, vcombine_u8(lo0, hi0));
            vst1q_u8(outdata + 16, vcombine_u8(lo1, hi1));
            vst1q_u8(outdata + 32, vcombine_u8(lo2, hi2));
            outdata += 48;

            CONVERT_I16_I12_BLOCK2(lo0, hi0, lo1);
            CONVERT_I16_I12_BLOCK2(hi1, lo2, hi2);
        }

        i -= 64;

        vst1q_u8(outdata +  0, vcombine_u8(lo0, hi0));
        vst1q_u8(outdata + 16, vcombine_u8(lo1, hi1));
        vst1q_u8(outdata + 32, vcombine_u8(lo2, hi2));
        outdata += 48;
    }

#undef CONVERT_I16_I12_BLOCK2

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
}

#undef TEMPLATE_FUNC_NAME
