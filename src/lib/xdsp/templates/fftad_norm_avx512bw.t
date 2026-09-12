static
void TEMPLATE_FUNC_NAME(fft_acc_t* __restrict p, unsigned fftsz, float scale, float corr, float* __restrict outa)
{
#ifdef USE_POLYLOG2
    WVLT_AVX512_POLYLOG2_DECL_CONSTS;
#else
    const __m512 log2_mul      = _mm512_set1_ps(WVLT_FASTLOG2_MUL);
    const __m512 log2_sub      = _mm512_set1_ps(WVLT_FASTLOG2_SUB);
#endif
    const __m512 vcorr         = _mm512_set1_ps(corr);
    const __m512 vscale        = _mm512_set1_ps(scale);

    const unsigned half = fftsz >> 1;

    for(unsigned i = 0; i < fftsz; i += 32)
    {
        __m512  m0 = _mm512_load_ps(p->f_mant + i +  0);
        __m512  m1 = _mm512_load_ps(p->f_mant + i + 16);

        __m512i p0 = _mm512_load_si512((__m512i*)(p->f_pwr + i +  0));
        __m512i p1 = _mm512_load_si512((__m512i*)(p->f_pwr + i + 16));

#ifdef USE_POLYLOG2
        __m512 apwr0, apwr1;
        WVLT_POLYLOG2F16(m0, apwr0);
        WVLT_POLYLOG2F16(m1, apwr1);
#else
        //wvlt_fastlog2
        __m512 l20 = _mm512_cvtepi32_ps(_mm512_castps_si512(m0));
        __m512 l21 = _mm512_cvtepi32_ps(_mm512_castps_si512(m1));
        __m512 apwr0 = _mm512_fmsub_ps(l20, log2_mul, log2_sub);
        __m512 apwr1 = _mm512_fmsub_ps(l21, log2_mul, log2_sub);
        //
#endif
        __m512 s0 = _mm512_add_ps(apwr0, _mm512_cvtepi32_ps(p0));
        __m512 s1 = _mm512_add_ps(apwr1, _mm512_cvtepi32_ps(p1));

        __m512 f0 = _mm512_fmadd_ps(vscale, s0, vcorr);
        __m512 f1 = _mm512_fmadd_ps(vscale, s1, vcorr);

        int32_t offset;

        if(i + 32 <= half)
        {
            offset = half;
        }
        else if(i >= half)
        {
            offset = - half;
        }
        else
        {
            offset = 0;
        }

        _mm512_store_ps(outa + i + offset +  0, f0);
        _mm512_store_ps(outa + i + offset + 16, f1);
    }
}

#undef TEMPLATE_FUNC_NAME
