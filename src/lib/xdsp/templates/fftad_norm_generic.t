static
void TEMPLATE_FUNC_NAME(fft_acc_t* __restrict p, unsigned fftsz, float scale, float corr, float* __restrict outa)
{
    for(unsigned i = 0; i < fftsz; ++i)
    {
#ifdef USE_POLYLOG2
        float apwr = wvlt_polylog2f(p->f_mant[i]);
#else
        float apwr = wvlt_fastlog2(p->f_mant[i]);
#endif
        int32_t aidx = p->f_pwr[i];
        float f = scale * (aidx + apwr) + corr;
        outa[i ^ (fftsz / 2)] = f;
    }
}

#undef TEMPLATE_FUNC_NAME
