static
void TEMPLATE_FUNC_NAME(fft_acc_t* __restrict p, unsigned fftsz, float scale, float corr, float* __restrict outa)
{
    for(unsigned i = 0; i < fftsz; ++i)
    {
        outa[i] = scale * p->f_mant[i] + corr;
    }
}

#undef TEMPLATE_FUNC_NAME
