static
void TEMPLATE_FUNC_NAME(const void *__restrict indata_p,
                        const void *__restrict ctrl_sin_p,
                        const void *__restrict ctrl_cos_p,
                        unsigned indatabsz,
                        void *__restrict outdata_p,
                        unsigned outdatabsz)
{
    unsigned i = indatabsz;
    if ((outdatabsz / 2) < i)
        i = (outdatabsz / 2);

    int16_t* phase    = (int16_t*)indata_p;
    int16_t* ctrl_sin = (int16_t*)ctrl_sin_p;
    int16_t* ctrl_cos = (int16_t*)ctrl_cos_p;
    int16_t* outdata  = (int16_t*)outdata_p;

    while(i >= 2)
    {
        const float ph = WVLT_SINCOS_I16_PHSCALE * *phase++;
        float ssin, scos;

        sincosf(ph, &ssin, &scos);
        *outdata++ = ssin * WVLT_SINCOS_I16_SCALE * *ctrl_sin++;
        *outdata++ = scos * WVLT_SINCOS_I16_SCALE * *ctrl_cos++;
        i -= 2;
    }
}

#undef TEMPLATE_FUNC_NAME
