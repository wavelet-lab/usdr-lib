static
void TEMPLATE_FUNC_NAME(const void *__restrict indata,
                        unsigned indatabsz,
                        void *__restrict outdata_0_p,
                        void *__restrict outdata_1_p,
                        unsigned outdatabsz)
{
    unsigned i = indatabsz;
    if ((outdatabsz) < i)
        i = (outdatabsz);

    int16_t* phase   = (int16_t*)indata;
    int16_t* sindata = (int16_t*)outdata_0_p;
    int16_t* cosdata = (int16_t*)outdata_1_p;

#include "wvlt_sincos_i16_neon.inc"

    while(i >= 16)
    {
        int16x8_t reg_phase = vld1q_s16(phase);
        int16x8_t reg_sin, reg_cos;

        WVLT_SINCOS(reg_phase, reg_sin, reg_cos);
        vst1q_s16(sindata, reg_sin);
        vst1q_s16(cosdata, reg_cos);

        sindata += 8;
        cosdata += 8;
        phase += 8;
        i -= 16;
    }

#undef WVLT_SINCOS

    while(i >= 2)
    {
        const float ph = WVLT_SINCOS_I16_PHSCALE * *phase++;
        float ssin, scos;
        sincosf(ph, &ssin, &scos);
        *sindata++ = ssin * WVLT_SINCOS_I16_SCALE;
        *cosdata++ = scos * WVLT_SINCOS_I16_SCALE;
        i -= 2;
    }
}

#undef TEMPLATE_FUNC_NAME
