    const __m128i vsign_sin = _mm_set1_epi16(sign_sin);
    const __m128i vsign_cos = _mm_set1_epi16(sign_cos);

    const __m128i ph_lo_mask = _mm_set_epi8(-1, -1, -1, -1, -1, -1, -1, -1, 13, 12, 9, 8, 5, 4, 1, 0);
    const __m128i ph_hi_mask = _mm_set_epi8(29, 28, 25, 24, 21, 20, 17, 16, -1, -1, -1, -1, -1, -1, -1, -1);

    const __m128i mpi_2v  = _mm_set1_epi32(-32768);
    const __m128i  pi_2v  = _mm_set1_epi32( 32767);
    const __m128i  onev   = _mm_set1_epi16(1);
    const __m128i  gainv  = _mm_set1_epi16(gain);

    typedef int32_t v4si __attribute__ ((vector_size (16)));
    union u_v4si { __m128i vect; v4si arr; };
    typedef union u_v4si u_v4si_t;

    u_v4si_t vphase0, vphase1;

    #include "wvlt_sincos_i16_ssse3.inc"

    while(i >= 8)
    {
        // fill the phases
        for(unsigned j = 0; j < 4; j++) {
            vphase0.arr[j] = phase;
            CHIRP_PHASE_CONDITION;
        }

        for(unsigned j = 0; j < 4; j++) {
            vphase1.arr[j] = phase;
            CHIRP_PHASE_CONDITION;
        }

#ifdef CHIRP_DEBUG_PRINT
        if(total_samples_count - i <= chirp_samples_count) {
            printf("---- SSSE3 ---- step_idx:%u PHASE:%d\n", step_idx, phase);
        }
#endif

        // _signed_ right shift to get hi word
        // 15 bits because input I32 range is [-PI; +PI), but WVLT_SINCOS I16 input range is [-PI/2; +PI/2)
        __m128i ph0 = _mm_srai_epi32(vphase0.vect, 15);
        __m128i ph1 = _mm_srai_epi32(vphase1.vect, 15);

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
