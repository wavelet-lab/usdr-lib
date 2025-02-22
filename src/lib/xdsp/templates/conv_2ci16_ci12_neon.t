static
void TEMPLATE_FUNC_NAME(const void *__restrict indata_0_p,
                        const void *__restrict indata_1_p,
                        unsigned indatabsz,
                        void *__restrict outdata_p,
                        unsigned outdatabsz)
{
    unsigned i = indatabsz;
    if ((outdatabsz * 4 / 3) < i)
        i = (outdatabsz * 4 / 3);

    const int16_t* indata_0 = (const int16_t*)indata_0_p;
    const int16_t* indata_1 = (const int16_t*)indata_1_p;
    uint8_t* outdata = (uint8_t*)outdata_p;

#include "conv_i16_i12_neon.inc"

#define CONVERT_2CI16_CI12_BLOCK(rlow, rmid, rhigh) \
    { \
        uint32x4x2_t rs = vzipq_u32(vld1q_u32((uint32_t*)indata_0), vld1q_u32((uint32_t*)indata_1)); \
        indata_0 += 8; \
        indata_1 += 8; \
    \
        int16x8_t i0 = vreinterpretq_s16_u32(rs.val[0]); \
        int16x8_t i1 = vreinterpretq_s16_u32(rs.val[1]); \
        CONVERT_I16_I12_BLOCK(i0, i1, rlow, rmid, rhigh); \
    }
// CONVERT_2CI16_CI12_BLOCK end

    uint8x8_t lo0, hi0, lo1, hi1, lo2, hi2;

    if(i >= 64)
    {
        CONVERT_2CI16_CI12_BLOCK(lo0, hi0, lo1);
        CONVERT_2CI16_CI12_BLOCK(hi1, lo2, hi2);

        for(; i >= 2*64; i -= 64)
        {
            vst1q_u8(outdata +  0, vcombine_u8(lo0, hi0));
            vst1q_u8(outdata + 16, vcombine_u8(lo1, hi1));
            vst1q_u8(outdata + 32, vcombine_u8(lo2, hi2));
            outdata += 48;

            CONVERT_2CI16_CI12_BLOCK(lo0, hi0, lo1);
            CONVERT_2CI16_CI12_BLOCK(hi1, lo2, hi2);
        }

        i -= 64;

        vst1q_u8(outdata +  0, vcombine_u8(lo0, hi0));
        vst1q_u8(outdata + 16, vcombine_u8(lo1, hi1));
        vst1q_u8(outdata + 32, vcombine_u8(lo2, hi2));
        outdata += 48;
    }

#undef CONVERT_2CI16_CI12_BLOCK

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
}
#undef TEMPLATE_FUNC_NAME
