static
void TEMPLATE_FUNC_NAME(fft_acc_t* __restrict p, uint16_t* __restrict d, unsigned fftsz)
{
    for (unsigned i = 0; i < fftsz; i += 32)
    {
        __m256i s0 = _mm256_load_si256((__m256i*)&d[i +  0]);
        __m256i s1 = _mm256_load_si256((__m256i*)&d[i + 16]);

        __m256i d0 = _mm256_cvtepu16_epi32(_mm256_castsi256_si128(s0));
        __m256i d1 = _mm256_cvtepu16_epi32(_mm256_extracti128_si256(s0, 1));
        __m256i d2 = _mm256_cvtepu16_epi32(_mm256_castsi256_si128(s1));
        __m256i d3 = _mm256_cvtepu16_epi32(_mm256_extracti128_si256(s1, 1));

        __m256 f0 = _mm256_cvtepi32_ps(d0);
        __m256 f1 = _mm256_cvtepi32_ps(d1);
        __m256 f2 = _mm256_cvtepi32_ps(d2);
        __m256 f3 = _mm256_cvtepi32_ps(d3);

        __m256 acc0 = _mm256_load_ps(&p->f_mant[i +  0]);
        __m256 acc1 = _mm256_load_ps(&p->f_mant[i +  8]);
        __m256 acc2 = _mm256_load_ps(&p->f_mant[i + 16]);
        __m256 acc3 = _mm256_load_ps(&p->f_mant[i + 24]);

        acc0 = _mm256_add_ps(acc0, f0);
        acc1 = _mm256_add_ps(acc1, f1);
        acc2 = _mm256_add_ps(acc2, f2);
        acc3 = _mm256_add_ps(acc3, f3);

        _mm256_store_ps(&p->f_mant[i +  0], acc0);
        _mm256_store_ps(&p->f_mant[i +  8], acc1);
        _mm256_store_ps(&p->f_mant[i + 16], acc2);
        _mm256_store_ps(&p->f_mant[i + 24], acc3);
    }
}

#undef TEMPLATE_FUNC_NAME
