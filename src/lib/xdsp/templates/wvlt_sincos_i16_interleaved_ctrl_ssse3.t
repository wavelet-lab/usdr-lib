static
void TEMPLATE_FUNC_NAME(const void *__restrict indata_p,
                        const void *__restrict ctrl_sin_p,
                        const void *__restrict ctrl_cos_p,
                        unsigned indatabsz,
                        void *__restrict outdata_p,
                        unsigned outdatabsz)
{
    unsigned i = indatabsz;
    if ((outdatabsz / 2) < i)
        i = (outdatabsz / 2);

    int16_t* phase    = (int16_t*)indata_p;
    int16_t* ctrl_sin = (int16_t*)ctrl_sin_p;
    int16_t* ctrl_cos = (int16_t*)ctrl_cos_p;
    int16_t* outdata  = (int16_t*)outdata_p;

#include "wvlt_sincos_i16_ssse3.inc"

    while(i >= 16)
    {
        __m128i reg_phase    = _mm_loadu_si128((__m128i*)(phase    + 0));
        __m128i reg_ctrl_sin = _mm_loadu_si128((__m128i*)(ctrl_sin + 0));
        __m128i reg_ctrl_cos = _mm_loadu_si128((__m128i*)(ctrl_cos + 0));
        __m128i reg_sin, reg_cos;

        WVLT_SINCOS(reg_phase, reg_sin, reg_cos);

        reg_sin = _mm_sign_epi16(reg_sin, reg_ctrl_sin);
        reg_cos = _mm_sign_epi16(reg_cos, reg_ctrl_cos);

        _mm_storeu_si128((__m128i*)(outdata + 0), _mm_unpacklo_epi16(reg_sin, reg_cos));
        _mm_storeu_si128((__m128i*)(outdata + 8), _mm_unpackhi_epi16(reg_sin, reg_cos));

        outdata += 16;
        phase += 8;
        ctrl_sin += 8;
        ctrl_cos += 8;
        i -= 16;
    }

#undef WVLT_SINCOS

    while(i >= 2)
    {
        const float ph = WVLT_SINCOS_I16_PHSCALE * *phase++;
        float ssin, scos;

        sincosf(ph, &ssin, &scos);
        *outdata++ = ssin * WVLT_SINCOS_I16_SCALE * *ctrl_sin++;
        *outdata++ = scos * WVLT_SINCOS_I16_SCALE * *ctrl_cos++;
        i -= 2;
    }
}

#undef TEMPLATE_FUNC_NAME
