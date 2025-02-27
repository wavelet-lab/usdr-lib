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

    const __m128i shfl_mask = _mm_set_epi8(15, 14, 11, 10, 7, 6, 3, 2, 13, 12, 9, 8, 5, 4, 1, 0);
    const __m128i mask0 = _mm_set1_epi32(0x8000);
    const __m128i mask1 = _mm_set1_epi32(0x10000);

#include "wvlt_sincos_i16_ssse3.inc"

    while(i >= 8)
    {
        // add delta
        __m128i vphase  = _mm_set1_epi32(phase);
        __m128i vphase0 = _mm_add_epi32(vdeltap0, vphase);
        __m128i vphase1 = _mm_add_epi32(vdeltap1, vphase);

        __m128i ph0 = _mm_srli_epi32(vphase0, 15);
        __m128i ph1 = _mm_srli_epi32(vphase1, 15);

        __m128i ph00 = _mm_add_epi32(ph0, mask0);
        __m128i ph10 = _mm_add_epi32(ph1, mask0);

        __m128i ph0m = _mm_and_si128(ph00, mask1);
        __m128i ph1m = _mm_and_si128(ph10, mask1);

        __m128i ph0x = _mm_cmpeq_epi32(ph0m, _mm_setzero_si128());
        __m128i ph1x = _mm_cmpeq_epi32(ph1m, _mm_setzero_si128());

        __m128i phm0 = _mm_shuffle_epi8(ph0, shfl_mask);
        __m128i phm1 = _mm_shuffle_epi8(ph1, shfl_mask);
        __m128i phm  = _mm_unpacklo_epi64(phm0, phm1); // packed phase
        __m128i phmn = _mm_sub_epi16(_mm_setzero_si128(), phm); //packed negative

        __m128i phx0 = _mm_shuffle_epi8(ph0x, shfl_mask);
        __m128i phx1 = _mm_shuffle_epi8(ph1x, shfl_mask);
        __m128i phx  = _mm_unpacklo_epi64(phx0, phx1); // packed non-invert mask

        __m128i vp = _mm_and_si128(phx, phm);
        __m128i vn = _mm_andnot_si128(phx, phmn);
        __m128i reg_phase = _mm_or_si128(vp, vn);

        __m128i reg_sin, reg_cos;
        WVLT_SINCOS(reg_phase, reg_sin, reg_cos);

        __m128i pcn = _mm_sub_epi16(_mm_setzero_si128(), reg_cos); // cos negative
        __m128i pcpm = _mm_and_si128(phx, reg_cos);
        __m128i pcnm = _mm_andnot_si128(phx, pcn);
        reg_cos = _mm_or_si128(pcpm, pcnm);

        //apply sign
        reg_sin = _mm_sign_epi16(reg_sin, vsign_sin);
        reg_cos = _mm_sign_epi16(reg_cos, vsign_cos);

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
