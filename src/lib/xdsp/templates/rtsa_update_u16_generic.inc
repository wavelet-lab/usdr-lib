#ifndef RTSA_UPDATE_U16_GEN_INC
#define RTSA_UPDATE_U16_GEN_INC


VWLT_ATTRIBUTE(optimize("-O3", "inline"))
static inline
void rtsa_charge_u16(rtsa_pwr_t* pwr, float charge_rate)
{
    float new_pwr = (float)(*pwr) * (1.0f - charge_rate) + CHARGE_NORM_COEF * charge_rate;
    if(new_pwr > MAX_RTSA_PWR) new_pwr = MAX_RTSA_PWR;
    *pwr = (rtsa_pwr_t)new_pwr;
}

VWLT_ATTRIBUTE(optimize("-O3", "inline"))
static inline
void rtsa_discharge_u16(rtsa_pwr_t* pwr, unsigned pw2_decay)
{
    unsigned delta = (*pwr >> pw2_decay) + ((unsigned)(DISCHARGE_NORM_COEF) >> pw2_decay);

    if(delta > *pwr)
        *pwr = 0;
    else
        *pwr -= delta;
}

#endif // RTSA_UPDATE_U16_GEN_INC
