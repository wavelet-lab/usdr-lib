static
void TEMPLATE_FUNC_NAME(int32_t *__restrict start_phase,
                        int32_t delta_phase,
                        bool inv_sin,
                        bool inv_cos,
                        int16_t *__restrict outdata,
                        unsigned iters)
{
    unsigned i = iters;
    int32_t phase = *start_phase;
    const int16_t sign_sin = inv_sin ? -1 : 1;
    const int16_t sign_cos = inv_cos ? -1 : 1;

    while(i > 0)
    {
        const float ph = WVLT_SINCOS_I16_PHSCALE * phase;
        float ssin, scos;

        sincosf(ph, &ssin, &scos);
        *outdata++ = ssin * WVLT_SINCOS_I16_SCALE * sign_sin;
        *outdata++ = scos * WVLT_SINCOS_I16_SCALE * sign_cos;

        phase += delta_phase;
        phase &= (WVLT_SINCOS_I16_TWO_PI - 1);
        --i;
    }

    *start_phase = phase;
}

#undef TEMPLATE_FUNC_NAME
