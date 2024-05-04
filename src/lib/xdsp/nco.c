// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include "nco.h"

#include <emmintrin.h>
#include <immintrin.h>
#include <smmintrin.h>
#include "attribute_switch.h"

#ifdef __SSSE3__

VWLT_ATTRIBUTE(optimize("O3", "inline"), target("ssse3"))
static void fsincos_ssse3(__m128i* pph, __m128i* psin, __m128i *pcos)
{
    __m128i ph = _mm_loadu_si128((__m128i*)pph);
    __m128i ph2 = _mm_mulhrs_epi16(ph, ph);
    __m128i phx1 = _mm_mulhrs_epi16(ph, _mm_set1_epi16(18705));
    __m128i phx3_c = _mm_mulhrs_epi16(ph, _mm_set1_epi16(-21166));
    __m128i phx5_c = _mm_mulhrs_epi16(ph, _mm_set1_epi16(2611));
    __m128i phx7_c = _mm_mulhrs_epi16(ph, _mm_set1_epi16(-152));
    __m128i ph4 = _mm_mulhrs_epi16(ph2, ph2);
    __m128i phx3 = _mm_mulhrs_epi16(ph2, phx3_c);
    __m128i phy2 = _mm_mulhrs_epi16(ph2, _mm_set1_epi16(-7656));
    __m128i phs0 = _mm_add_epi16(ph, phx1);
    __m128i phc0 = _mm_sub_epi16(_mm_set1_epi16(32767), ph2);
    __m128i phs1 = _mm_add_epi16(phs0, phx3);
    __m128i phc1 = _mm_add_epi16(phc0, phy2);
    __m128i ph6 = _mm_mulhrs_epi16(ph4, ph2);
    __m128i phx5 = _mm_mulhrs_epi16(ph4, phx5_c);
    __m128i phy48 = _mm_mulhrs_epi16(ph4, _mm_set1_epi16(30));
    __m128i phy4 = _mm_mulhrs_epi16(ph4, _mm_set1_epi16(8311));
    // dummy
    __m128i phy6 = _mm_mulhrs_epi16(ph6, _mm_set1_epi16(-683));
    __m128i phx7 = _mm_mulhrs_epi16(ph6, phx7_c);
    __m128i phy8 = _mm_mulhrs_epi16(ph4, phy48);
    __m128i phs2 = _mm_add_epi16(phs1, phx5);
    __m128i phc2 = _mm_add_epi16(phc1, phy4);
    // dummy
    // dummy
    __m128i phs3 = _mm_add_epi16(phs2, phx7);
    __m128i phc3 = _mm_add_epi16(phc2, phy6);
    __m128i phc4 = _mm_add_epi16(phc3, phy8);

    _mm_storeu_si128((__m128i*)psin, phs3);
    _mm_storeu_si128((__m128i*)pcos, phc4);
}

 VWLT_ATTRIBUTE(optimize("O3", "inline"), target("ssse3"))
static void calc_vco_iq_ssse3(int32_t phase, int32_t delta, __m128i* psin, __m128i *pcos)
{
    __m128i dp  = _mm_set1_epi32(delta);
    __m128i dpp = _mm_add_epi32(dp, dp);
    __m128i dp0 = _mm_and_si128(dp, _mm_set_epi32(0xFFFFFFFF, 0x00000000, 0xFFFFFFFF, 0x00000000));  //+1
    __m128i dp1 = _mm_and_si128(dpp, _mm_set_epi32(0xFFFFFFFF, 0xFFFFFFFF, 0x00000000, 0x00000000)); //+2

    __m128i dp3 = _mm_add_epi32(dp1, dp0); // +3 +2 +1 +0
    __m128i dpq = _mm_add_epi32(dpp, dpp); // +4 +4 +4 +4


    // TODO calc mask on absolute phase
    __m128i ini = _mm_set1_epi32(phase);
    __m128i p0b = _mm_add_epi32(ini, dp3); // 3 2 1 0
    __m128i p0c = _mm_add_epi32(p0b, dpq); // 7 6 5 4

    __m128i ph0 = _mm_srli_epi32(p0b, 15);
    __m128i ph1 = _mm_srli_epi32(p0c, 15);

    __m128i ph00 = _mm_add_epi32(ph0, _mm_set1_epi32(0x8000));
    __m128i ph10 = _mm_add_epi32(ph1, _mm_set1_epi32(0x8000));

    __m128i ph0m = _mm_and_si128(ph00, _mm_set1_epi32(0x10000));
    __m128i ph1m = _mm_and_si128(ph10, _mm_set1_epi32(0x10000));

    __m128i ph0x = _mm_cmpeq_epi32(ph0m, _mm_setzero_si128());
    __m128i ph1x = _mm_cmpeq_epi32(ph1m, _mm_setzero_si128());

    __m128i phm0 = _mm_shuffle_epi8(ph0, _mm_set_epi8(15, 14, 11, 10, 7, 6, 3, 2, 13, 12, 9, 8, 5, 4, 1, 0));
    __m128i phm1 = _mm_shuffle_epi8(ph1, _mm_set_epi8(15, 14, 11, 10, 7, 6, 3, 2, 13, 12, 9, 8, 5, 4, 1, 0));
    __m128i phm  = _mm_unpacklo_epi64(phm0, phm1); // packed phase
    __m128i phmn = _mm_sub_epi16(_mm_setzero_si128(), phm); //packed negative

    __m128i phx0 = _mm_shuffle_epi8(ph0x, _mm_set_epi8(15, 14, 11, 10, 7, 6, 3, 2, 13, 12, 9, 8, 5, 4, 1, 0));
    __m128i phx1 = _mm_shuffle_epi8(ph1x, _mm_set_epi8(15, 14, 11, 10, 7, 6, 3, 2, 13, 12, 9, 8, 5, 4, 1, 0));
    __m128i phx  = _mm_unpacklo_epi64(phx0, phx1); // packed non-invert mask

    __m128i vp = _mm_and_si128(phx, phm);
    __m128i vn = _mm_andnot_si128(phx, phmn);
    __m128i vphase = _mm_or_si128(vp, vn);

    __m128i pc;
    fsincos_ssse3(&vphase, (__m128i*)psin, &pc);

    __m128i pcn = _mm_sub_epi16(_mm_setzero_si128(), pc); // cos negative
    __m128i pcpm = _mm_and_si128(phx, pc);
    __m128i pcnm = _mm_andnot_si128(phx, pcn);
    __m128i pcf = _mm_or_si128(pcpm, pcnm);

    _mm_storeu_si128((__m128i*)pcos, pcf);
}

VWLT_ATTRIBUTE(optimize("O3", "inline"), target("ssse3"))
static int32_t do_shift_up_ssse3(int32_t inphase, int32_t delta,
                                 const int16_t* iqbuf, unsigned csamples,
                                 int16_t* out)
{
    int32_t phase = inphase;
    for (unsigned n = 0; n < csamples; n += 8, phase += (delta << 3)) {
        __m128i piq0 = _mm_loadu_si128((__m128i*)&iqbuf[2*n]);      // q3 i3 q2 i2 q1 i1 q0 i0
        __m128i piq1 = _mm_loadu_si128((__m128i*)&iqbuf[2*n + 8]);
        __m128i vcoi, vcoq;

        calc_vco_iq_ssse3(phase, delta, &vcoq, &vcoi);

        // de-shuffle
        __m128i phm0 = _mm_shuffle_epi8(piq0, _mm_set_epi8(15, 14, 11, 10, 7, 6, 3, 2, 13, 12, 9, 8, 5, 4, 1, 0));
        __m128i phm1 = _mm_shuffle_epi8(piq1, _mm_set_epi8(15, 14, 11, 10, 7, 6, 3, 2, 13, 12, 9, 8, 5, 4, 1, 0));
        __m128i vi  = _mm_unpacklo_epi64(phm0, phm1); // i vec
        __m128i vq  = _mm_unpackhi_epi64(phm0, phm1); // q vec

        __m128i oi0 = _mm_mulhrs_epi16(vcoi, vi);
        __m128i oi1 = _mm_mulhrs_epi16(vcoq, vq);
        __m128i oq0 = _mm_mulhrs_epi16(vcoi, vq);
        __m128i oq1 = _mm_mulhrs_epi16(vcoq, vi);

        __m128i oi = _mm_sub_epi16(oi0, oi1);
        __m128i oq = _mm_add_epi16(oq0, oq1);

        // shuffle back
        __m128i oiq0 = _mm_unpacklo_epi64(oi, oq); // q3 q2 q1 q0  i3 i2 i1 i0
        __m128i oiq1 = _mm_unpackhi_epi64(oi, oq);
        __m128i oiq2 = _mm_shuffle_epi8(oiq0, _mm_set_epi8(15, 14, 7, 6, 13, 12, 5, 4, 11, 10, 3, 2, 9, 8, 1, 0));
        __m128i oiq3 = _mm_shuffle_epi8(oiq1, _mm_set_epi8(15, 14, 7, 6, 13, 12, 5, 4, 11, 10, 3, 2, 9, 8, 1, 0));

        _mm_storeu_si128((__m128i*)&out[2*n], oiq2);
        _mm_storeu_si128((__m128i*)&out[2*n + 8], oiq3);
    }
    return phase;
}



/*
#define MULI16_NORM(x, y) (((int32_t)(x) * (y)) >> 15)
//#define MULI16_NORM(x, y) (((((int32_t)(x) * (y)) >> 14) + 1) >> 1)

static void __attribute__((optimize("O3"))) fsincos(int16_t ph, int16_t* psin, int16_t *pcos)
{
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
    // dummy
    int16_t phy6 = MULI16_NORM(ph6, -683);
    int16_t phx7 = MULI16_NORM(ph6, phx7_c);
    int16_t phy8 = MULI16_NORM(ph4, phy48);
    int16_t phs2 = phs1 + phx5; int phc2 = phc1 + phy4;
    // dummy
    // dummy
    int16_t phs3 = phs2 + phx7; int phc3 = phc2 + phy6;
    int16_t phc4 = phc3 + phy8;

    *psin = phs3;
    *pcos = phc4;
}

static void __attribute__((optimize("O3"))) calc_vco_iq(int32_t phase, int16_t* i, int16_t* q)
{
    int32_t ph = phase >> 15;

    if (((ph & 0x18000) == 0x10000) || ((ph & 0x18000) == 0x08000)) {
        fsincos(65536 - ph, q, i);
        *i = -*i;
    } else {
        fsincos(ph, q, i);
    }
}

void calc_vco_iq(int32_t phase, int16_t* i, int16_t* q)
{
    float p = -M_PI * phase / INT32_MIN;
    float c, s;
    sincosf(p, &s, &c);
    *i = c * INT16_MAX;
    *q = s * INT16_MAX;
}

int32_t do_shift_up(int32_t inphase, int32_t delta, int16_t* iqbuf, unsigned csamples)
{
    int32_t phase = inphase;
    for (unsigned n = 0; n < csamples; n++, phase += delta) {
        int16_t vcoi, vcoq;
        int16_t i = iqbuf[2*n], q = iqbuf[2*n + 1];
        int16_t oi, oq;
        calc_vco_iq(phase, &vcoi, &vcoq);
        // (oi, oq) = (vcoi, vcoq) * (i, q)
        oi = ((int32_t)vcoi * i - (int32_t)vcoq * q) >> 15;
        oq = ((int32_t)vcoi * q + (int32_t)vcoq * i) >> 15;
        iqbuf[2*n+0] = oi;
        iqbuf[2*n+1] = oq;
    }
    return phase;
}
*/

#endif

int32_t nco_shift(int32_t inphase,
                  int32_t delta,
                  const int16_t* iqbuf,
                  unsigned csamples,
                  int16_t* out)
{
#ifdef __SSSE3__
    return do_shift_up_ssse3(inphase, delta, iqbuf, csamples, out);
#else
    return 0;
#endif
}

