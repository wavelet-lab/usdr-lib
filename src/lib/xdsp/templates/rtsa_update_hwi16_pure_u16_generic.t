#include "rtsa_update_u16_generic.inc"

VWLT_ATTRIBUTE(optimize("-O3", "inline"))
static inline
void rtsa_charge_pure_u16(rtsa_pwr_t* pwr, uint16_t pw2_raise)
{
    uint16_t delta = - (*pwr >> pw2_raise) + (uint16_t)((unsigned)(CHARGE_NORM_COEF) >> pw2_raise);

    if(*pwr > MAX_RTSA_PWR - delta)
        *pwr = MAX_RTSA_PWR;
    else
        *pwr += delta;
}

VWLT_ATTRIBUTE(optimize("-O3"))
static
void TEMPLATE_FUNC_NAME(uint16_t* __restrict in, unsigned fft_size,
                        fft_rtsa_data_t* __restrict rtsa_data,
                        UNUSED float scale, UNUSED float corr, fft_diap_t diap, const rtsa_hwi16_consts_t* __restrict hwi16_consts)
{
#ifdef USE_POLYLOG2
    wvlt_log2f_fn_t wvlt_log2f_fn = wvlt_polylog2f;
#else
    wvlt_log2f_fn_t wvlt_log2f_fn = wvlt_fastlog2;
#endif

    const fft_rtsa_settings_t * st = &rtsa_data->settings;
    const unsigned rtsa_depth = st->rtsa_depth;

    const unsigned decay_rate_pw2 =
        (unsigned)(wvlt_log2f_fn((float)st->charging_frame * st->decay_coef) + 0.5f);

    const unsigned raise_rate_pw2 =
        (unsigned)(wvlt_log2f_fn((float)st->charging_frame / st->raise_coef) + 0.5f);

    for(unsigned i = diap.from; i < diap.to; ++i)
    {
        rtsa_pwr_t* pwr = rtsa_data->pwr + i * rtsa_depth;

        for(unsigned j = 0; j < rtsa_depth; ++j)
        {
            rtsa_discharge_u16(&pwr[j], decay_rate_pw2);
        }

        uint16_t tmp = (in[i] - hwi16_consts->c0);
        tmp = tmp >> hwi16_consts->shr0;
        tmp *= (uint16_t)hwi16_consts->org_scale;
        int16_t pi = (tmp >> hwi16_consts->shr1) - hwi16_consts->c1;
        pi = abs(pi);

        if(pi > (rtsa_depth - 1)) pi = (rtsa_depth - 1);

        rtsa_charge_pure_u16(&pwr[pi], raise_rate_pw2 - hwi16_consts->ndivs_for_dB);
    }
}

#undef TEMPLATE_FUNC_NAME
