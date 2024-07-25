#ifndef RTSA_UPDATE_U16_GEN_INC
#define RTSA_UPDATE_U16_GEN_INC


VWLT_ATTRIBUTE(optimize("-O3", "inline"))
static inline
void rtsa_charge_u16(rtsa_pwr_t* pwr, float charge_rate)
{
    uint16_t delta = (uint16_t)((-(float)(*pwr) + CHARGE_NORM_COEF) * charge_rate);

    if(delta > MAX_RTSA_PWR - *pwr)
        *pwr = MAX_RTSA_PWR;
    else
        *pwr += delta;
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
