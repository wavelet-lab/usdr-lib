#include "spiext.h"


unsigned spiext_make_data_reg(unsigned memoutsz,
                              const void* pout)
{
    unsigned spout = 0;
    for (unsigned k = 0; k < memoutsz; k++) {
        spout |= ((unsigned)(((__u8*)pout)[k])) << (8 * (k /*+ (4 - memoutsz)*/));
    }
    return spout;
}

void spiext_parse_data_reg(unsigned spin,
                           unsigned meminsz,
                           void* pin)
{
    for (unsigned k = 0; k < meminsz; k++) {
        ((__u8*)pin)[k] = spin >> (8 * k);
    }
}
