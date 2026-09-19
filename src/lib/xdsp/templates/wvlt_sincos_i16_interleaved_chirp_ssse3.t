#include <stdio.h>

static
void TEMPLATE_FUNC_NAME(int32_t *__restrict start_phase, int32_t *__restrict start_delta_phase,
                        int32_t *__restrict delta_phase,
                        int32_t chirp_steps_cnt,
                        int16_t gain,
                        bool inv_sin,
                        bool inv_cos,
                        int16_t *__restrict outdata,
                        unsigned iters)
{
    unsigned i = iters;

    int32_t phase = *start_phase;
    int32_t delta_phase0 = delta_phase[0];
    int32_t delta_phase1 = delta_phase[1];

    const int16_t sign_sin = inv_sin ? -1 : 1;
    const int16_t sign_cos = inv_cos ? -1 : 1;

    const int32_t chirp_step = ((int64_t)delta_phase1 - (int64_t)delta_phase0) / chirp_steps_cnt;
    int32_t dphase = *start_delta_phase;

    const __m128i vsign_sin = _mm_set1_epi16(sign_sin);
    const __m128i vsign_cos = _mm_set1_epi16(sign_cos);

    const __m128i vchirps0 = _mm_set_epi32(3 * chirp_step, 2 * chirp_step, 1 * chirp_step, 0 * chirp_step);
    const __m128i vchirps1 = _mm_set_epi32(7 * chirp_step, 6 * chirp_step, 5 * chirp_step, 4 * chirp_step);

    const __m128i ph_lo_mask = _mm_set_epi8(-1, -1, -1, -1, -1, -1, -1, -1, 13, 12, 9, 8, 5, 4, 1, 0);
    const __m128i ph_hi_mask = _mm_set_epi8(29, 28, 25, 24, 21, 20, 17, 16, -1, -1, -1, -1, -1, -1, -1, -1);

    const __m128i mpi_2v  = _mm_set1_epi32(-32768);
    const __m128i  pi_2v  = _mm_set1_epi32( 32767);
    const __m128i  onev   = _mm_set1_epi16(1);
    const __m128i  gainv  = _mm_set1_epi16(gain);

    #include "wvlt_sincos_i16_ssse3.inc"

    while(i >= 8)
    {
        // add delta

        const __m128i vdelta_phase = _mm_set1_epi32(dphase);

        __m128i delta_ph0 = _mm_add_epi32(vchirps0, vdelta_phase);
        delta_ph0 = _mm_add_epi32(delta_ph0, _mm_slli_si128(delta_ph0, 4));
        delta_ph0 = _mm_add_epi32(delta_ph0, _mm_slli_si128(delta_ph0, 8));

        __m128i vphase0 = _mm_set1_epi32(phase);
        vphase0 = _mm_add_epi32(vphase0, delta_ph0);
        phase = _mm_extract_epi32(vphase0, 3);

        __m128i delta_ph1 = _mm_add_epi32(vchirps1, vdelta_phase);
        delta_ph1 = _mm_add_epi32(delta_ph1, _mm_slli_si128(delta_ph1, 4));
        delta_ph1 = _mm_add_epi32(delta_ph1, _mm_slli_si128(delta_ph1, 8));

        __m128i vphase1 = _mm_set1_epi32(phase);
        vphase1 = _mm_add_epi32(vphase1, delta_ph1);
        phase = _mm_extract_epi32(vphase1, 3);

        dphase += 8 * chirp_step;
        if(dphase > delta_phase1)
            dphase = delta_phase0;
        else if(dphase < delta_phase0)
            dphase = delta_phase1;


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

        //apply amplitude normalization
        reg_sin = _mm_mulhrs_epi16(reg_sin, gainv);
        reg_cos = _mm_mulhrs_epi16(reg_cos, gainv);

        // interleave & store
        _mm_storeu_si128((__m128i*)(outdata + 0), _mm_unpacklo_epi16(reg_sin, reg_cos));
        _mm_storeu_si128((__m128i*)(outdata + 8), _mm_unpackhi_epi16(reg_sin, reg_cos));

        outdata += 16;
        i -= 8;
    }

    #undef WVLT_SINCOS

    #include "wvlt_sincos_i16_interleaved_chirp_generic.inc"

    *start_phase = phase;
    *start_delta_phase = dphase;
}

#undef TEMPLATE_FUNC_NAME
