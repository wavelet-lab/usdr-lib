static
void TEMPLATE_FUNC_NAME(fft_acc_t* __restrict p,  unsigned fftsz)
{
    for(unsigned i = 0; i < fftsz; ++i)
    {
        p->f_mant[i] = 0.0;
    }
}

#undef TEMPLATE_FUNC_NAME
