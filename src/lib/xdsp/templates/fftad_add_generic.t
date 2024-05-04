static
void TEMPLATE_FUNC_NAME(fft_acc_t* __restrict p, wvlt_fftwf_complex* __restrict d, unsigned fftsz)
{
    const uint32_t fnoexp = ~(0xffu << 23);
    const uint32_t fexp0 = 127u << 23;
    const uint32_t expcorr = 127;

    union uvar_t {
        uint32_t a;
        float f;
    };

    for (unsigned i = 0; i < fftsz; ++i)
    {
        float en = d[i][0] * d[i][0] + d[i][1] * d[i][1];
        float enz = en + p->mine;

        union uvar_t zmpy, z;
        zmpy.f = enz * p->f_mant[i];
        z.a = (fnoexp & zmpy.a) | fexp0;

        int32_t az = zmpy.a >> 23;
        int32_t azsum = az + p->f_pwr[i];
        int32_t azc = azsum - expcorr;

        p->f_mant[i] = z.f;
        p->f_pwr[i] = azc;
    }
}

#undef TEMPLATE_FUNC_NAME
