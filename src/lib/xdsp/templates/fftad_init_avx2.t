static
void TEMPLATE_FUNC_NAME(fft_acc_t* __restrict p,  unsigned fftsz)
{
    __m256 e1 = _mm256_set1_ps(1.0);
    __m256i d1 = _mm256_set1_epi32(0);

    for (unsigned i = 0; i < fftsz; i += 8) {
        _mm256_store_ps(p->f_mant + i, e1);
        _mm256_store_si256((__m256i*)(p->f_pwr + i), d1);
    }
}

#undef TEMPLATE_FUNC_NAME
