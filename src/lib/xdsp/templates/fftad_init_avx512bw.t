static
void TEMPLATE_FUNC_NAME(fft_acc_t* __restrict p,  unsigned fftsz)
{
    __m512 e1 = _mm512_set1_ps(1.0);
    __m512i d1 = _mm512_set1_epi32(0);

    for (unsigned i = 0; i < fftsz; i += 16) {
        _mm512_store_ps(p->f_mant + i, e1);
        _mm512_store_si512((__m512i*)(p->f_pwr + i), d1);
    }
}

#undef TEMPLATE_FUNC_NAME
