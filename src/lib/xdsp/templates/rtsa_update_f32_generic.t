VWLT_ATTRIBUTE(optimize("-O3", "inline"))
static inline
void rtsa_charge_f32(rtsa_pwr_t* pwr, float charge_rate)
{
    *pwr = *pwr * (1.0f - charge_rate) + CHARGE_NORM_COEF * charge_rate;
    if(*pwr > MAX_RTSA_PWR) *pwr = MAX_RTSA_PWR;
}

VWLT_ATTRIBUTE(optimize("-O3", "inline"))
static inline
void rtsa_discharge_f32(rtsa_pwr_t* pwr, float discharge_rate)
{
    *pwr = *pwr * (1.0f - discharge_rate) - DISCHARGE_NORM_COEF * discharge_rate;
    if(*pwr < 0) *pwr = 0.0f;
}

VWLT_ATTRIBUTE(optimize("-O3"))
static
void TEMPLATE_FUNC_NAME(wvlt_fftwf_complex* __restrict in, unsigned fft_size,
                        fft_rtsa_data_t* __restrict rtsa_data,
                        float fcale_mpy, float mine, float corr)
{
    const fft_rtsa_settings_t * st = &rtsa_data->settings;
    const unsigned rtsa_depth = st->rtsa_depth;
    const float charge_rate = (float)st->raise_coef * st->divs_for_dB / st->charging_frame;
    const float discharge_rate = 1.0f / st->charging_frame / st->decay_coef;

    for(unsigned i = 0; i < fft_size; ++i)
    {
        float p = fcale_mpy * log2f(in[i][0]*in[i][0] + in[i][1]*in[i][1] + mine) + corr;
        p -= st->upper_pwr_bound;
        p = fabs(p);
        p *= st->divs_for_dB;
        if(p > (float)(rtsa_depth - 1) - 0.5f) p = (float)(rtsa_depth - 1) - 0.5f;

        float pi_lo = (unsigned)p;
        float pi_hi = pi_lo + 1.0f;

        rtsa_pwr_t* pwr = rtsa_data->pwr + i * rtsa_depth;
        rtsa_charge_f32(&pwr[(unsigned)pi_hi], charge_rate * (p - pi_lo));
        rtsa_charge_f32(&pwr[(unsigned)pi_lo], charge_rate * (pi_hi - p));

        for(unsigned j = 0; j < rtsa_depth; ++j)
        {
            rtsa_discharge_f32(&pwr[j], discharge_rate);
        }
    }
}

#undef TEMPLATE_FUNC_NAME
