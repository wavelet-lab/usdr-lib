static
void TEMPLATE_FUNC_NAME(fft_acc_t* __restrict p, uint16_t* __restrict d, unsigned fftsz)
{
    for (unsigned i = 0; i < fftsz; ++i)
    {
        p->f_mant[i] += d[i];
    }
}

#undef TEMPLATE_FUNC_NAME
