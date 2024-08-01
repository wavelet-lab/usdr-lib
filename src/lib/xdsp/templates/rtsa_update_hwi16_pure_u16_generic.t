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
                        float scale, UNUSED float corr, fft_diap_t diap)
{
#ifdef USE_POLYLOG2
    wvlt_log2f_fn_t wvlt_log2f_fn = wvlt_polylog2f;
#else
    wvlt_log2f_fn_t wvlt_log2f_fn = wvlt_fastlog2;
#endif

    const fft_rtsa_settings_t * st = &rtsa_data->settings;
    const unsigned rtsa_depth = st->rtsa_depth;

    const unsigned decay_rate_pw2 =
        (unsigned)wvlt_log2f_fn(st->charging_frame) + (unsigned)wvlt_log2f_fn(st->decay_coef);

    const unsigned raise_rate_pw2 =
        (unsigned)wvlt_log2f_fn(st->charging_frame) - (unsigned)wvlt_log2f_fn(st->raise_coef);

    const uint16_t nfft = (uint16_t)wvlt_log2f_fn(fft_size);
    const uint16_t c1 = 2 * HWI16_SCALE_COEF * nfft;
    //const uint16_t c2 = - HWI16_CORR_COEF * ( 1.f - 1.f / wvlt_fastlog2(10));
    static const uint16_t c2 = (- HWI16_CORR_COEF) * 0.69897f;

    const uint16_t nscale = (uint16_t)wvlt_log2f_fn(scale + 0.5);
    const uint16_t ndivs_for_dB = (uint16_t)wvlt_log2f_fn(st->divs_for_dB + 0.5);
    const uint16_t shr0 = nscale;
    const uint16_t shr1 = HWI16_SCALE_N2_COEF - nscale > ndivs_for_dB ? HWI16_SCALE_N2_COEF - nscale - ndivs_for_dB : 16;

    for(unsigned i = diap.from; i < diap.to; ++i)
    {
        uint16_t tmp = (in[i] - c1);
        tmp = tmp >> shr0;
        tmp *= (uint16_t)scale;
        int16_t pi = (tmp >> shr1) - (c2 << ndivs_for_dB);

        pi -= st->upper_pwr_bound;
        pi = abs(pi);

        if(pi > (rtsa_depth - 1)) pi = (rtsa_depth - 1);

        rtsa_pwr_t* pwr = rtsa_data->pwr + i * rtsa_depth;
        rtsa_charge_pure_u16(&pwr[pi], raise_rate_pw2 - ndivs_for_dB);

        for(unsigned j = 0; j < rtsa_depth; ++j)
        {
            rtsa_discharge_u16(&pwr[j], decay_rate_pw2);
        }
    }
}

#undef TEMPLATE_FUNC_NAME
