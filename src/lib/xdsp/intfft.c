// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

//
// FFT calculation--integer

#include "intfft.h"

#include <memory.h>
#include <emmintrin.h>
#include <tmmintrin.h>
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <math.h>

#if 0
void fft_prepare(unsigned n, struct fft_plane** out)
{
    struct fft_plane* plane = (struct fft_plane*)malloc(sizeof(struct fft_plane));

    //calc twiddle factor
    int16_t* mem = NULL;
    posix_memalign((void**)&mem, 64, 2 * 2 * 2 * n);

    plane->n = n;
    plane->buffer = mem;
    plane->factors = mem + n * 2;

    for (unsigned i = 0; i < n; i++) {
        float t = 2 * M_PI * i / n;
        float i, q;
        sincosf(t, &q, &i);

        int16_t inti = (int16_t)(0x7fff * i);
        int16_t intq = (int16_t)(0x7fff * q);
        plane->factors[i] = inti;
        plane->factors[n + i] = intq;
    }

    plane->tw2 = 0;
    for (unsigned k = n; k != 0; k>>=1) {
        plane->tw2++;
    }

    float raise = 1.0f;
    const float raise_factor = 1.0f + sqrtf(2.0f);

    for (unsigned k = n; k < plane->tw2; k++) {
        raise *= raise_factor;

        plane->normstages[k] = 0;
        while (raise > 1) {
            plane->normstages[k]++;
            raise /= 2;
        }
    }

    *out = plane;
}

static void fft_twiddle2(const int16_t wre,
                         const int16_t win,
                         const int16_t *ire,
                         const int16_t *iim,
                         int16_t *ore,
                         int16_t *oim,
                         unsigned normbits)
{
    int sre = (int)ire[1] * (int)wre  + (int)iim[1] * (int)win;
    int sim = (int)iim[1] * (int)wre  - (int)ire[1] * (int)win;
    sre = ((sre >> 14) + 1) >> 1;
    sim = ((sim >> 14) + 1) >> 1;

    int oi0 = ( (int)ire[0] + sre ) >> normbits;
    int oq0 = ( (int)iim[0] + sim ) >> normbits;
    int oi1 = ( (int)ire[0] - sre ) >> normbits;
    int oq1 = ( (int)iim[0] - sim ) >> normbits;

    ore[0] = oi0;
    ore[1] = oi1;
    oim[0] = oq0;
    oim[1] = oq1;
}

void fft_exec(struct fft_plane* plane,
              const int16_t *ire,
              const int16_t *iim,
              int16_t *ore,
              int16_t *oim)
{
    for (unsigned k = 0; k < plane->tw2; k++) {
        const int16_t *pire = (k == 0) ? ire : plane->buffer;
        const int16_t *piim = (k == 0) ? iim : plane->buffer + plane->n;
        int16_t *pore = (k == plane->tw2 - 1) ? ore : plane->buffer;
        int16_t *poim = (k == plane->tw2 - 1) ? oim : plane->buffer + plane->n;

        for (unsigned z = 0; z < plane->n / 2; z++) {
            //int16_t lpire[2] = pire[z]

            fft_twiddle2(wre, win,
                         pire, piim, pore, poim, plane->normstages[k]);
        }
    }
}

#endif

#if 0   //unused foo
#ifdef __SSSE3__

__attribute__((optimize("O3", "inline"), target("ssse3")))
static void calc_fftc1d16_sse(__m128i i0, __m128i q0,
                              __m128i i1, __m128i q1,
                              __m128i ic16, __m128i qc16,
                              __m128i ic8, __m128i qc8,
                              __m128i ic4, __m128i qc4,
                              __m128i ic2, __m128i qc2,
                              __m128i* oi0, __m128i* oq0,
                              __m128i* oi1, __m128i* oq1)
{
    // oi0 = i0 + (i1*cos(2pi k/n) + q1*sin(2pi k/n))
    // oq0 = q0 + (q1*cos(2pi k/n) - i1*sin(2pi k/n))

    // oi1 = i0 - (i1*cos(2pi k/n) + q1*sin(2pi k/n))
    // oq1 = q0 - (q1*cos(2pi k/n) - i1*sin(2pi k/n))

    // Stage - 1 (16)
    // ([i0, i1]; [q0, q1])

    // Scaling i0, q0
    __m128i i0d = _mm_srli_epi16(i0, 1);
    __m128i q0d = _mm_srli_epi16(q0, 1);
    __m128i i1c = _mm_mulhrs_epi16(i1, ic16);
    __m128i q1s = _mm_mulhrs_epi16(q1, qc16);
    __m128i i1s = _mm_mulhrs_epi16(i1, qc16);
    __m128i q1c = _mm_mulhrs_epi16(q1, ic16);

    __m128i sumai = _mm_add_epi16(i1c, q1s);  // 0.5
    __m128i sumaq = _mm_sub_epi16(q1c, i1s);  // 0.5

    __m128i av0i = _mm_add_epi16(i0d, sumai);  // 0.5
    __m128i av0q = _mm_add_epi16(q0d, sumaq);  // 0.5
    __m128i av1i = _mm_sub_epi16(i0d, sumai);  // 0.5
    __m128i av1q = _mm_sub_epi16(q0d, sumaq);  // 0.5

    // Stage 2 (8)
    // av0 [7,  6,  5,  4,  3,  2,  1,  0]
    // av1 [15, 14, 13, 12, 11, 10, 9,  8]
    __m128i at1i = _mm_unpackhi_epi16(av0i, av1i); // 15, 7, 14, 6, 13, 5, 12, 4
    __m128i at1q = _mm_unpackhi_epi16(av0q, av1q); // 15, 7, 14, 6, 13, 5, 12, 4
    __m128i at0i = _mm_unpacklo_epi16(av0i, av1i); // 11, 3, 10, 2, 9, 1, 8, 0
    __m128i at0q = _mm_unpacklo_epi16(av0q, av1q); // 11, 3, 10, 2, 9, 1, 8, 0

    __m128i at0id = _mm_srli_epi16(at0i, 1);
    __m128i at0qd = _mm_srli_epi16(at0q, 1);
    __m128i ai2c = _mm_mulhrs_epi16(at1i, ic8);
    __m128i aq2s = _mm_mulhrs_epi16(at1q, qc8);
    __m128i ai2s = _mm_mulhrs_epi16(at1i, qc8);
    __m128i aq2c = _mm_mulhrs_epi16(at1q, ic8);

    __m128i sumbi = _mm_add_epi16(ai2c, aq2s);  // 0.5
    __m128i sumbq = _mm_sub_epi16(aq2c, ai2s);  // 0.5

    __m128i bv0i = _mm_add_epi16(at0id, sumbi);  // 0.5
    __m128i bv0q = _mm_add_epi16(at0qd, sumbq);  // 0.5
    __m128i bv1i = _mm_sub_epi16(at0id, sumbi);  // 0.5
    __m128i bv1q = _mm_sub_epi16(at0qd, sumbq);  // 0.5

    // Stage 3 (4)
    __m128i bt1i = _mm_unpackhi_epi32(bv0i, bv1i); // 15, 7, 11, 3, 14, 6, 10, 2
    __m128i bt1q = _mm_unpackhi_epi32(bv0q, bv1q); // 15, 7, 11, 3, 14, 6, 10, 2
    __m128i bt0i = _mm_unpacklo_epi32(bv0i, bv1i); // 13, 5, 9, 1, 12, 4, 8, 0
    __m128i bt0q = _mm_unpacklo_epi32(bv0q, bv1q); // 13, 5, 9, 1, 12, 4, 8, 0

    __m128i bt0id = _mm_srli_epi16(bt0i, 1);
    __m128i bt0qd = _mm_srli_epi16(bt0q, 1);
    __m128i bi2c = _mm_mulhrs_epi16(bt1i, ic4);
    __m128i bq2s = _mm_mulhrs_epi16(bt1q, qc4);
    __m128i bi2s = _mm_mulhrs_epi16(bt1i, qc4);
    __m128i bq2c = _mm_mulhrs_epi16(bt1q, ic4);

    __m128i sumci = _mm_add_epi16(bi2c, bq2s);  // 0.5
    __m128i sumcq = _mm_sub_epi16(bq2c, bi2s);  // 0.5

    __m128i cv0i = _mm_add_epi16(bt0id, sumci);  // 0.5
    __m128i cv0q = _mm_add_epi16(bt0qd, sumcq);  // 0.5
    __m128i cv1i = _mm_sub_epi16(bt0id, sumci);  // 0.5
    __m128i cv1q = _mm_sub_epi16(bt0qd, sumcq);  // 0.5

    // Stage 4 (2)
    __m128i ct1i = _mm_unpackhi_epi64(cv1i, cv0i); // 15, 7, 11, 3, 13, 5, 9, 1
    __m128i ct1q = _mm_unpackhi_epi64(cv1q, cv0q); // 15, 7, 11, 3, 13, 5, 9, 1
    __m128i ct0i = _mm_unpacklo_epi64(cv1i, cv0i); // 14, 6, 10, 2, 12, 4, 8, 0
    __m128i ct0q = _mm_unpacklo_epi64(cv1q, cv0q); // 14, 6, 10, 2, 12, 4, 8, 0

    __m128i ct0id = _mm_srli_epi16(ct0i, 1);
    __m128i ct0qd = _mm_srli_epi16(ct0q, 1);
    __m128i ci2c = _mm_mulhrs_epi16(ct1i, ic2);
    __m128i cq2s = _mm_mulhrs_epi16(ct1q, qc2);
    __m128i ci2s = _mm_mulhrs_epi16(ct1i, qc2);
    __m128i cq2c = _mm_mulhrs_epi16(ct1q, ic2);

    __m128i sumdi = _mm_add_epi16(ci2c, cq2s);  // 0.5
    __m128i sumdq = _mm_sub_epi16(cq2c, ci2s);  // 0.5

    __m128i dv0i = _mm_add_epi16(ct0id, sumdi);  // 0.5
    __m128i dv0q = _mm_add_epi16(ct0qd, sumdq);  // 0.5
    __m128i dv1i = _mm_sub_epi16(ct0id, sumdi);  // 0.5
    __m128i dv1q = _mm_sub_epi16(ct0qd, sumdq);  // 0.5

    _mm_store_si128(oi0, dv0i);
    _mm_store_si128(oq0, dv0q);
    _mm_store_si128(oi1, dv1i);
    _mm_store_si128(oq1, dv1q);
}

#endif
#endif
