static
void TEMPLATE_FUNC_NAME(fft_acc_t* __restrict p, unsigned fftsz, float scale, float corr, float* __restrict outa)
{
    for(unsigned i = 0; i < fftsz; ++i)
    {
        float apwr = wvlt_fastlog2(p->f_mant[i]);
        int32_t aidx = p->f_pwr[i];
        float f = scale * (aidx + apwr) + corr;
        outa[i ^ (fftsz / 2)] = f;
    }
}

#undef TEMPLATE_FUNC_NAME
