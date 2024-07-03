static
void TEMPLATE_FUNC_NAME(fft_acc_t* __restrict p, unsigned fftsz, float scale, float corr, float* __restrict outa)
{
#ifdef USE_POLYLOG2
    WVLT_POLYLOG2_DECL_CONSTS;
#else
    const __m256 log2_mul      = _mm256_set1_ps(WVLT_FASTLOG2_MUL);
    const __m256 log2_sub      = _mm256_set1_ps(WVLT_FASTLOG2_SUB);
#endif
    const __m256 vcorr         = _mm256_set1_ps(corr);
    const __m256 vscale        = _mm256_set1_ps(scale);
    const __m256i sh           = _mm256_setr_epi32(0, 1, 4, 5, 2, 3, 6, 7);

    const unsigned half = fftsz >> 1;

    for(unsigned i = 0; i < fftsz; i += 16)
    {
        __m256  m0 = _mm256_load_ps(p->f_mant + i + 0);
        __m256  m1 = _mm256_load_ps(p->f_mant + i + 8);
        __m256i p0 = _mm256_load_si256((__m256i*)(p->f_pwr + i + 0));
        __m256i p1 = _mm256_load_si256((__m256i*)(p->f_pwr + i + 8));

#ifdef USE_POLYLOG2
        __m256 apwr0, apwr1;
        WVLT_POLYLOG2F8(m0, apwr0);
        WVLT_POLYLOG2F8(m1, apwr1);
#else
        //wvlt_fastlog2
        __m256 l20 = _mm256_cvtepi32_ps(_mm256_castps_si256(m0));
        __m256 l21 = _mm256_cvtepi32_ps(_mm256_castps_si256(m1));
        __m256 apwr0 = _mm256_fmsub_ps(l20, log2_mul, log2_sub);
        __m256 apwr1 = _mm256_fmsub_ps(l21, log2_mul, log2_sub);
        //
#endif
        __m256 s0 = _mm256_add_ps(apwr0, _mm256_cvtepi32_ps(p0));
        __m256 s1 = _mm256_add_ps(apwr1, _mm256_cvtepi32_ps(p1));

        __m256 f0 = _mm256_fmadd_ps(vscale, s0, vcorr);
        __m256 f1 = _mm256_fmadd_ps(vscale, s1, vcorr);

        f0 = _mm256_permutevar8x32_ps(f0, sh);
        f1 = _mm256_permutevar8x32_ps(f1, sh);

        int32_t offset;

        if(i + 16 <= half)
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

        _mm256_store_ps(outa + i + offset + 0, f0);
        _mm256_store_ps(outa + i + offset + 8, f1);
    }
}

#undef TEMPLATE_FUNC_NAME
