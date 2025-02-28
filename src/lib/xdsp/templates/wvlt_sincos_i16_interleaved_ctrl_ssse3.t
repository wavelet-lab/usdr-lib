static
void TEMPLATE_FUNC_NAME(int32_t *__restrict start_phase,
                        int32_t delta_phase,
                        bool inv_sin,
                        bool inv_cos,
                        int16_t *__restrict outdata,
                        unsigned iters)
{
    unsigned i = iters;
    int32_t phase = *start_phase;
    const int16_t sign_sin = inv_sin ? -1 : 1;
    const int16_t sign_cos = inv_cos ? -1 : 1;

    const __m128i vsign_sin = _mm_set1_epi16(sign_sin);
    const __m128i vsign_cos = _mm_set1_epi16(sign_cos);
    const __m128i vdeltap0 = _mm_set_epi32(3 * delta_phase, 2 * delta_phase, 1 * delta_phase, 0 * delta_phase);
    const __m128i vdeltap1 = _mm_set_epi32(7 * delta_phase, 6 * delta_phase, 5 * delta_phase, 4 * delta_phase);

    const __m128i ph_lo_mask = _mm_set_epi8(-1, -1, -1, -1, -1, -1, -1, -1, 13, 12, 9, 8, 5, 4, 1, 0);
    const __m128i ph_hi_mask = _mm_set_epi8(29, 28, 25, 24, 21, 20, 17, 16, -1, -1, -1, -1, -1, -1, -1, -1);

    const __m128i mpi_2v  = _mm_set1_epi32(-32768);
    const __m128i  pi_2v  = _mm_set1_epi32( 32767);
    const __m128i  onev   = _mm_set1_epi16(1);

#include "wvlt_sincos_i16_ssse3.inc"

    while(i >= 8)
    {
        // add delta
        __m128i vphase  = _mm_set1_epi32(phase);
        __m128i vphase0 = _mm_add_epi32(vdeltap0, vphase);
        __m128i vphase1 = _mm_add_epi32(vdeltap1, vphase);

        // _signed_ right shift to get hi word
        // 15 bits because input I32 range is [-PI; +PI), but WVLT_SINCOS I16 input range is [-PI/2; +PI/2)
        __m128i ph0 = _mm_srai_epi32(vphase0, 15);
        __m128i ph1 = _mm_srai_epi32(vphase1, 15);

        // INT32 input ranges < INT16_MIN and > INT16_MAX should be inverted
        __m128i rflag0 = _mm_or_si128(_mm_cmpgt_epi32(ph0, pi_2v), _mm_cmplt_epi32(ph0, mpi_2v));
        __m128i rflag1 = _mm_or_si128(_mm_cmpgt_epi32(ph1, pi_2v), _mm_cmplt_epi32(ph1, mpi_2v));
        __m128i sign = _mm_or_si128(_mm_shuffle_epi8(rflag0, ph_lo_mask), _mm_shuffle_epi8(rflag1, ph_hi_mask));

        // normalize sign from (-1;0) to (-1;+1) : x*2 + 1
        sign = _mm_add_epi16(_mm_slli_epi16(sign, 1), onev);

        // pack phase low int16 words (already shifted >> 15)
        __m128i reg_phase = _mm_or_si128(_mm_shuffle_epi8(ph0, ph_lo_mask), _mm_shuffle_epi8(ph1, ph_hi_mask));
        __m128i reg_sin, reg_cos;
        WVLT_SINCOS(reg_phase, reg_sin, reg_cos);

        // apply sign - internal & external
        reg_sin = _mm_sign_epi16(reg_sin, sign);
        reg_cos = _mm_sign_epi16(reg_cos, sign);
        reg_sin = _mm_sign_epi16(reg_sin, vsign_sin);
        reg_cos = _mm_sign_epi16(reg_cos, vsign_cos);

        // interleave & store
        _mm_storeu_si128((__m128i*)(outdata + 0), _mm_unpacklo_epi16(reg_sin, reg_cos));
        _mm_storeu_si128((__m128i*)(outdata + 8), _mm_unpackhi_epi16(reg_sin, reg_cos));

        outdata += 16;
        phase += (delta_phase << 3);
        i -= 8;
    }

#undef WVLT_SINCOS

    while(i > 0)
    {
        const float ph = WVLT_SINCOS_I32_PHSCALE * phase;
        float ssin, scos;

        sincosf(ph, &ssin, &scos);
        *outdata++ = ssin * WVLT_SINCOS_I16_SCALE * sign_sin;
        *outdata++ = scos * WVLT_SINCOS_I16_SCALE * sign_cos;

        phase += delta_phase;
        --i;
    }

    *start_phase = phase;
}

#undef TEMPLATE_FUNC_NAME
