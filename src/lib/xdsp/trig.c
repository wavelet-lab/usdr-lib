// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include "trig.h"
#include "attribute_switch.h"

// Note! -32768 * -32768 == -32768, i.e. -1 * -1 == -1
#define MULI16_NORM(x, y) (int16_t)(((((int32_t)(x) * (y)) >> 14) + 1) >> 1)

VWLT_ATTRIBUTE(optimize("O3"))
void isincos_generic(const int16_t *pph, int16_t* psin, int16_t *pcos)
{
    int16_t ph = *pph;
#ifdef CORR_32768
    int16_t amsk = (ph == -32768) ? -1 : 0;
#endif
    int16_t ph2 = MULI16_NORM(ph, ph);
    int16_t phx1 = MULI16_NORM(ph, 18705);
    int16_t phx3_c = MULI16_NORM(ph, -21166);
    int16_t phx5_c = MULI16_NORM(ph, 2611);
    int16_t phx7_c = MULI16_NORM(ph, -152);
    int16_t ph4 = MULI16_NORM(ph2, ph2);
    int16_t phx3 = MULI16_NORM(ph2, phx3_c);
    int16_t phy2 = MULI16_NORM(ph2, -7656);
    int16_t phs0 = ph + phx1;    int16_t phc0 = 32767 - ph2;
    int16_t phs1 = phs0 + phx3;  int16_t phc1 = phc0 + phy2;
    int16_t ph6 = MULI16_NORM(ph4, ph2);
    int16_t phx5 = MULI16_NORM(ph4, phx5_c);
    int16_t phy48 = MULI16_NORM(ph4, 30);
    int16_t phy4 = MULI16_NORM(ph4, 8311);
#ifdef CORR_32768
    int16_t ccorr = amsk & (-57);
    int16_t scorr = amsk & (-28123);
#else
    // dummy
#endif
    int16_t phy6 = MULI16_NORM(ph6, -683);
    int16_t phx7 = MULI16_NORM(ph6, phx7_c);
    int16_t phy8 = MULI16_NORM(ph4, phy48);
    int16_t phs2 = phs1 + phx5;
    int16_t phc2 = phc1 + phy4;
#ifdef CORR_32768
    phs2 += scorr;
    phc2 += ccorr;
#else
    // dummy
    // dummy
#endif
    int16_t phs3 = phs2 + phx7; int16_t phc3 = phc2 + phy6;
    int16_t phc4 = phc3 + phy8;

    *psin = phs3;
    *pcos = phc4;
}
