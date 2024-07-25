static
void TEMPLATE_FUNC_NAME(fft_acc_t* __restrict p,  unsigned fftsz)
{
    __m256 f = _mm256_setzero_ps();

    for (unsigned i = 0; i < fftsz; i += 16)
    {
        _mm256_store_ps(p->f_mant + i + 0, f);
        _mm256_store_ps(p->f_mant + i + 8, f);
    }
}

#undef TEMPLATE_FUNC_NAME
