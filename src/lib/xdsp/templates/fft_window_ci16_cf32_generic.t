static
void TEMPLATE_FUNC_NAME(wvlt_fftwi16_complex* __restrict in, unsigned fftsz, float* __restrict wnd,
                        wvlt_fftwf_complex* __restrict out)
{
    for(unsigned i = 0; i < fftsz; ++i)
    {
        out[i][0] = wnd[i] * in[i][0]; //wnd[i] must be normalized!
        out[i][1] = wnd[i] * in[i][1];
    }
}

#undef TEMPLATE_FUNC_NAME
