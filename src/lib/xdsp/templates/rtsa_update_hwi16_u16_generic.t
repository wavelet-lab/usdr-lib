#include "rtsa_update_u16_generic.inc"

VWLT_ATTRIBUTE(optimize("-O3"))
static
void TEMPLATE_FUNC_NAME(uint16_t* __restrict in, unsigned fft_size,
                        fft_rtsa_data_t* __restrict rtsa_data,
                        float scale, float corr, fft_diap_t diap, UNUSED const rtsa_hwi16_consts_t* hwi16_consts)
{
#ifdef USE_POLYLOG2
    wvlt_log2f_fn_t wvlt_log2f_fn = wvlt_polylog2f;
#else
    wvlt_log2f_fn_t wvlt_log2f_fn = wvlt_fastlog2;
#endif

    const fft_rtsa_settings_t * st = &rtsa_data->settings;
    const unsigned rtsa_depth = st->rtsa_depth;
    const float charge_rate = (float)st->raise_coef * st->divs_for_dB / st->charging_frame;
    const unsigned decay_rate_pw2 = (unsigned)(wvlt_log2f_fn(st->charging_frame * st->decay_coef) + 0.5);

    for(unsigned i = diap.from; i < diap.to; ++i)
    {
        float p = scale * (float)in[i] + corr;

        p -= st->upper_pwr_bound;
        p = fabs(p);
        p *= st->divs_for_dB;
        if(p > (float)(rtsa_depth - 1) - 0.5f) p = (float)(rtsa_depth - 1) - 0.5f;

        rtsa_pwr_t* pwr = rtsa_data->pwr + i * rtsa_depth;

#ifdef USE_RTSA_ANTIALIASING
        float pi_lo = (unsigned)p;
        float pi_hi = pi_lo + 1.0f;

        rtsa_charge_u16(&pwr[(unsigned)pi_hi], charge_rate * (p - pi_lo));
        rtsa_charge_u16(&pwr[(unsigned)pi_lo], charge_rate * (pi_hi - p));
#else
        rtsa_charge_u16(&pwr[(unsigned)p], charge_rate);
#endif
        for(unsigned j = 0; j < rtsa_depth; ++j)
        {
            rtsa_discharge_u16(&pwr[j], decay_rate_pw2);
        }
    }
}

#undef TEMPLATE_FUNC_NAME
