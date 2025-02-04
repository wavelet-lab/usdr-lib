static
void TEMPLATE_FUNC_NAME(const void *__restrict indata,
                        unsigned indatabsz,
                        void *__restrict outdata_0_p,
                        void *__restrict outdata_1_p,
                        unsigned outdatabsz)
{
    unsigned i = indatabsz;
    if ((outdatabsz) < i)
        i = (outdatabsz);

    int16_t* phase   = (int16_t*)indata;
    int16_t* sindata = (int16_t*)outdata_0_p;
    int16_t* cosdata = (int16_t*)outdata_1_p;

    while(i >= 2)
    {
        const float ph = WVLT_SINCOS_I16_PHSCALE * *phase++;
        float ssin, scos;
        sincosf(ph, &ssin, &scos);
        *sindata++ = ssin * WVLT_SINCOS_I16_SCALE;
        *cosdata++ = scos * WVLT_SINCOS_I16_SCALE;
        i -= 2;
    }
}

#undef TEMPLATE_FUNC_NAME
