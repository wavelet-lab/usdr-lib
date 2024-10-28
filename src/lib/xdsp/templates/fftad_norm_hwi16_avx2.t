static
void TEMPLATE_FUNC_NAME(fft_acc_t* __restrict p, unsigned fftsz, float scale, float corr, float* __restrict outa)
{
    const __m256 vcorr         = _mm256_set1_ps(corr);
    const __m256 vscale        = _mm256_set1_ps(scale);

    for(unsigned i = 0; i < fftsz; i += 32)
    {
        __m256  acc0 = _mm256_load_ps(p->f_mant + i +  0);
        __m256  acc1 = _mm256_load_ps(p->f_mant + i +  8);
        __m256  acc2 = _mm256_load_ps(p->f_mant + i + 16);
        __m256  acc3 = _mm256_load_ps(p->f_mant + i + 24);

        __m256 f0 = _mm256_fmadd_ps(vscale, acc0, vcorr);
        __m256 f1 = _mm256_fmadd_ps(vscale, acc1, vcorr);
        __m256 f2 = _mm256_fmadd_ps(vscale, acc2, vcorr);
        __m256 f3 = _mm256_fmadd_ps(vscale, acc3, vcorr);

        _mm256_store_ps(outa + i +  0, f0);
        _mm256_store_ps(outa + i +  8, f1);
        _mm256_store_ps(outa + i + 16, f2);
        _mm256_store_ps(outa + i + 24, f3);
    }
}

#undef TEMPLATE_FUNC_NAME
