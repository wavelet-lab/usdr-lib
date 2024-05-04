// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include <stdint.h>
#include <string.h>
#include <assert.h>
#include "filter.h"

#include <emmintrin.h>
#include <immintrin.h>
#include <smmintrin.h>

#include "attribute_switch.h"
#include "conv_filter.h"

#define CACHE_LINE  64u

enum {
    FD_INTERLEAVE = 1, // I0 Q0 I1 Q1 ...
    FD_SEPARATED  = 2, // I0 I1 I2 .... Q0 Q1 Q2 ...
    FD_INTERP     = 8,
};

#if 0
typedef void (*conv_int16_t)(const int16_t *__restrict data,
                             const int16_t *__restrict conv,
                             int16_t *__restrict out,
                             unsigned count,
                             unsigned decim_bits,
                             unsigned flen);
#endif

struct filter_data {
    const int16_t* filter_data;
    filter_function_t func;
    const int16_t* user_data;

    unsigned blocksz;
    unsigned decim_inter;
    unsigned ftaps;
    unsigned flags;
    unsigned history_movebsz;

    char _align[CACHE_LINE - 5 * sizeof(unsigned) - 2 * sizeof(const int16_t*) - sizeof(void*)];

    // Cache aligned
    int16_t history_data[0];
};

#if 0
// GENERIC FUNCTION
//----------------------------------------------------------------------------

VWLT_ATTRIBUTE(optimize("O3"))
void conv_int16(const int16_t *__restrict data,
                const int16_t *__restrict conv,
                int16_t *__restrict out,
                unsigned count,
                unsigned decim_bits,
                unsigned flen)
{
    unsigned i, n;
    for (n = 0; n < count; n += (1 << decim_bits)) {
        int32_t acc = 0;
        for (i = 0; i < flen; i++) {
            acc += (int32_t)data[n + i] * (int32_t)conv[i];
        }
        out[(n >> decim_bits) + 0] = acc >> 15;
    }
}


VWLT_ATTRIBUTE(optimize("O3"))
void conv_int16_p(const int16_t *__restrict data,
                  const int16_t *__restrict conv,
                  int16_t *__restrict out,
                  unsigned count,
                  unsigned interp,
                  unsigned flen)
{
    unsigned i, n, z;
    const unsigned shift = (interp == 1) ? 15 :
                           (interp == 2) ? 14 :
                           (interp == 4) ? 13 :
                           (interp == 8) ? 12 :
                           (interp ==16) ? 11 : 10;

    for (n = 0; n < count; n++) {
        for (z = 0; z < interp; z++) {
            int32_t acc = 0;
            for (i = 0; i < flen; i++) {
                acc += (int32_t)data[n + i] * (int32_t)conv[i + z * flen];
            }
            out[interp * n + z] = acc >> shift;
        }
    }
}

VWLT_ATTRIBUTE(optimize("O3"))
void conv_int16i(const int16_t *__restrict data,
                 const int16_t *__restrict conv,
                 int16_t *__restrict out,
                 unsigned count,
                 unsigned decim_bits,
                 unsigned flen)
{
    unsigned i, n;
    for (n = 0; n < count; n += (2 << decim_bits)) {
        int32_t acc[2] = {0, 0};
        for (i = 0; i < flen; i++) {
            acc[0] += (int32_t)data[n + 2 * i + 0] * (int32_t)conv[i];
            acc[1] += (int32_t)data[n + 2 * i + 1] * (int32_t)conv[i];
        }
        out[(n >> decim_bits) + 0] = acc[0] >> 15;
        out[(n >> decim_bits) + 1] = acc[1] >> 15;
    }
}

// polyphase interpoltion
void conv_int16_pi(const int16_t *__restrict data,
                   const int16_t *__restrict conv,
                   int16_t *__restrict out,
                   unsigned count,
                   unsigned interp,
                   unsigned flen)
{
    const unsigned shift = (interp == 1) ? 15 :
                           (interp == 2) ? 14 :
                           (interp == 4) ? 13 :
                           (interp == 8) ? 12 :
                           (interp ==16) ? 11 : 10;
    unsigned i, n, z;
    for (n = 0; n < count; n += 2) {
        for (z = 0; z < interp; z++) {
            int32_t acc[2] = {0, 0};
            for (i = 0; i < flen; i++) {
                acc[0] += (int32_t)data[n + 2 * i + 0] * (int32_t)conv[i + z * flen];
                acc[1] += (int32_t)data[n + 2 * i + 1] * (int32_t)conv[i + z * flen];
            }
            out[interp * n + 2 * z + 0] = acc[0] >> shift;
            out[interp * n + 2 * z + 1] = acc[1] >> shift;
        }
    }
}


// OPTIMIZED FUNCTION
//----------------------------------------------------------------------------

VWLT_ATTRIBUTE(optimize("O3"))
void conv_int16_sse(const int16_t *__restrict data,
                    const int16_t *__restrict conv,
                    int16_t *__restrict out,
                    unsigned count,
                    unsigned decim_bits,
                    unsigned flen)
{
    unsigned i, n;
    for (n = 0; n < count; n += (1 << decim_bits)) {
        __m128i acc = _mm_setzero_si128();

        for (i = 0; i < flen; i += 8) {
            __m128i c = _mm_load_si128((__m128i*)&conv[i]);
            __m128i d = _mm_loadu_si128((__m128i*)&data[n + i]);
            __m128i s = _mm_madd_epi16(d, c);
            acc = _mm_add_epi32(acc, s);
        }

        __m128i pshi = _mm_unpackhi_epi64(acc, acc);      // qs3 is2  .  qs3 is2
        __m128i pshm = _mm_add_epi32(acc, pshi);          //  x    x  . qss1 iss0

        __m128i nnnn = _mm_srli_epi64(pshm, 32);
        __m128i pshz = _mm_add_epi32(pshm, nnnn);

        __m128i pshnorm = _mm_srli_epi32(pshz, 15);

        out[(n >> decim_bits)] = _mm_extract_epi16(pshnorm, 0);
    }
}


VWLT_ATTRIBUTE(optimize("O3", "inline"), target("ssse3"))
void conv_int16i_sse(const int16_t *__restrict data,
                     const int16_t *__restrict conv,
                     int16_t *__restrict out,
                     unsigned count,
                     unsigned decim_bits,
                     unsigned flen)
{
    unsigned i, n;
    for (n = 0; n < count; n += (2 << decim_bits)) {
        __m128i acc[2] = { _mm_setzero_si128(), _mm_setzero_si128() };

        for (i = 0; i < flen; i += 8) {
            __m128i c = _mm_load_si128((__m128i*)&conv[i]);
            __m128i d0 = _mm_loadu_si128((__m128i*)&data[n + 2 * i + 0]);  // q3 i3 q2 i2 . q1 i1 q0 i0
            __m128i d1 = _mm_loadu_si128((__m128i*)&data[n + 2 * i + 8]);  // q7 i7 q6 i6 . q5 i5 q4 i4

            __m128i phm0 = _mm_shuffle_epi8(d0, _mm_set_epi8(15, 14, 11, 10, 7, 6, 3, 2, 13, 12, 9, 8, 5, 4, 1, 0));
            __m128i phm1 = _mm_shuffle_epi8(d1, _mm_set_epi8(15, 14, 11, 10, 7, 6, 3, 2, 13, 12, 9, 8, 5, 4, 1, 0));

            __m128i vi  = _mm_unpacklo_epi64(phm0, phm1); // i vec
            __m128i vq  = _mm_unpackhi_epi64(phm0, phm1); // q vec

            __m128i dsi = _mm_madd_epi16(vi, c);          //  qs1 is1 . qs0 is0
            __m128i dsq = _mm_madd_epi16(vq, c);          //  qs3 is3 . qs2 is2

            acc[0] = _mm_add_epi32(acc[0], dsi);
            acc[1] = _mm_add_epi32(acc[1], dsq);
        }

        __m128i ps0 = _mm_unpacklo_epi32(acc[0], acc[1]); // qs1 is1 qs0 is0
        __m128i ps1 = _mm_unpackhi_epi32(acc[0], acc[1]); // qs3 is3 qs2 is2

        __m128i psh = _mm_add_epi32(ps0, ps1);            // qss1 iss1 . qss0 iss0
        __m128i pshi = _mm_unpackhi_epi64(psh, psh);      // qss1 iss1 . qss1 iss1
        __m128i pshm = _mm_add_epi32(psh, pshi);          //  x    x   . qsss isss
        __m128i pshnorm = _mm_srli_epi32(pshm, 15);       // x x . x x . x q . x i

        out[(n >> decim_bits) + 0] = _mm_extract_epi16(pshnorm, 0);
        out[(n >> decim_bits) + 1] = _mm_extract_epi16(pshnorm, 2);
    }
}

VWLT_ATTRIBUTE(optimize("O3", "inline"), target("avx2"))
void conv_int16_avx(const int16_t *__restrict data,
                    const int16_t *__restrict conv,
                    int16_t *__restrict out,
                    unsigned count,
                    unsigned decim_bits,
                    unsigned flen)
{
    unsigned i, n;
    for (n = 0; n < count; n += (1 << decim_bits)) {
        __m128i acc = _mm_setzero_si128();

        for (i = 0; i < flen; i += 8) {
            __m128i c = _mm_load_si128((__m128i*)&conv[i]);
            __m128i d = _mm_loadu_si128((__m128i*)&data[n + i]);
            __m128i s = _mm_madd_epi16(d, c);
            acc = _mm_add_epi32(acc, s);
        }

        __m128i pshi = _mm_unpackhi_epi64(acc, acc);      // qs3 is2  .  qs3 is2
        __m128i pshm = _mm_add_epi32(acc, pshi);          //  x    x  . qss1 iss0

        __m128i nnnn = _mm_srli_epi64(pshm, 32);
        __m128i pshz = _mm_add_epi32(pshm, nnnn);

        __m128i pshnorm = _mm_srli_epi32(pshz, 15);

        out[(n >> decim_bits)] = _mm_extract_epi16(pshnorm, 0);
    }
}

#ifndef __EMSCRIPTEN__
VWLT_ATTRIBUTE(optimize("O3", "inline"), target("avx2"))
void conv_int16i_avx(const int16_t *__restrict data,
                     const int16_t *__restrict conv,
                     int16_t *__restrict out,
                     unsigned count,
                     unsigned decim_bits,
                     unsigned flen)
{
    unsigned i, n;
    __m256i msk = _mm256_set_epi8(13, 12, 15, 14, 9, 8, 11, 10, 5, 4, 7, 6, 1, 0, 3, 2,
                                  13, 12, 15, 14, 9, 8, 11, 10, 5, 4, 7, 6, 1, 0, 3, 2);
    for (n = 0; n < count; n += (2 << decim_bits)) {
        __m256i acc[2] = { _mm256_setzero_si256(), _mm256_setzero_si256() };

        for (i = 0; i < flen; i += 16) {
            __m256i c = _mm256_load_si256((__m256i*)&conv[i]);
            __m256i d0 = _mm256_loadu_si256((__m256i*)&data[n + 2 * i + 0]);  // q7 i7 q6 i6 q5 i5 q4 i4 . q3 i3 q2 i2 q1 i1 q0 i0
            __m256i d1 = _mm256_loadu_si256((__m256i*)&data[n + 2 * i + 16]); // qF iF qE iE qD iD qC iC . qB iB qA iA q9 i9 q8 i8

            __m256i d0sh = _mm256_shuffle_epi8(d0, msk);                      // i7 q7 i6 q6 i5 q5 i4 q4 . i3 q3 i2 q2 i1 q1 i0 q0
            __m256i phmX = _mm256_blend_epi16(d0sh, d1, 0xAA);                // qF q7 qE q6 qD q5 qC q4   qB q3 qA q2 q9 q1 q8 q0
            __m256i phm0 = _mm256_blend_epi16(d0sh, d1, 0x55);                // i7 iF i6 iE i5 iD i4 iC   i3 iB i2 iA i1 i9 i0 i8
            __m256i phm1 = _mm256_shuffle_epi8(phmX, msk);                    // q7 qF q6 qE q5 qD q4 qC   q3 qB q2 qA q1 q9 q0 q8

            __m256i dsi = _mm256_madd_epi16(phm0, c); // is7 is6 is5 is4 is3 is2 is1 is0
            __m256i dsq = _mm256_madd_epi16(phm1, c);

            acc[0] = _mm256_add_epi32(acc[0], dsi);
            acc[1] = _mm256_add_epi32(acc[1], dsq);
        }

        __m256i ps0 = _mm256_unpacklo_epi32(acc[0], acc[1]); // qs5 is5 qs4 is4  qs1 is1 qs0 is0
        __m256i ps1 = _mm256_unpackhi_epi32(acc[0], acc[1]); // qs7 is7 qs5 is5  qs3 is3 qs2 is2
        __m256i psh = _mm256_add_epi32(ps0, ps1);            // qss3 iss3 . qss2 iss2 . qss1 iss1 . qss0 iss0
        __m256i pshi = _mm256_permute4x64_epi64 (psh, _MM_SHUFFLE(3, 2, 3, 2));  //     qss3 iss3 . qss2 iss2
        __m256i pshm = _mm256_add_epi32(psh, pshi);                              //     qsN1 isN1 . qsN0 isN0
        __m256i pshl = _mm256_unpackhi_epi64(pshm, pshm);
        __m256i pshk = _mm256_add_epi32(pshl, pshm);
        __m256i pshnorm = _mm256_srli_epi32(pshk, 13);                           //     x x . x x . x q . x i

        out[(n >> decim_bits) + 0] = _mm256_extract_epi16(pshnorm, 0);
        out[(n >> decim_bits) + 1] = _mm256_extract_epi16(pshnorm, 2);
    }
}
#endif

#endif

#if 0
enum functype {
    OPT_GENERIC,
    OPT_SSSE3,
    OPT_AVX2,
};
#endif

static_assert((sizeof(struct filter_data) % CACHE_LINE) == 0, "filer_data_t should be cache aligned");



filter_data_t* filter_data_alloc(unsigned origblksz,
                                const int16_t* pfilter,
                                unsigned filer_taps,
                                unsigned decim_inter,
                                unsigned flags)
{
    unsigned tapssz = (filer_taps * sizeof(int16_t) + (CACHE_LINE-1)) & (~(CACHE_LINE-1));
    unsigned datasz = (origblksz * sizeof(int16_t) + (CACHE_LINE-1)) & (~(CACHE_LINE-1));
    filter_data_t* f;

    // Not supported yet
    if (flags & FDAF_SEPARATED)
        return NULL;

    int res = posix_memalign((void**)&f, CACHE_LINE, sizeof(filter_data_t) +
                             3 * tapssz + datasz);
    if (res) {
        return NULL;
    }

    int16_t* tdata = (int16_t*)f + ((sizeof(filter_data_t) + 2 * tapssz + datasz) >> 1);
    f->filter_data = tdata;
    f->user_data = (int16_t*)f + (sizeof(filter_data_t) >> 1) + filer_taps;
    f->ftaps = filer_taps;
    f->blocksz = origblksz;
    f->decim_inter = decim_inter;
    f->flags = 0;
    f->history_movebsz = filer_taps * (unsigned)sizeof(int16_t);
    if (flags & FDAF_INTERLEAVE) {
        f->flags |= FD_INTERLEAVE;
        f->user_data += filer_taps;
        f->history_movebsz <<= 1;
    }
    if (flags & FDAF_INTERPOLATE) {
        f->flags |= FD_INTERP;
        f->ftaps = (filer_taps + decim_inter - 1) / decim_inter;

        assert(tapssz >= f->ftaps * decim_inter * sizeof(int16_t));
    }

    memset(tdata, 0, tapssz);
    memset(f->history_data, 0, 2 * tapssz);
    memcpy(tdata, pfilter, filer_taps * sizeof(int16_t));

#if 0
    unsigned opttype = OPT_GENERIC;
    if (!(flags & FDAF_NO_VECTOR)) {
#ifdef __EMSCRIPTEN__
        opttype = OPT_SSSE3;
#else
        __builtin_cpu_init();
        if (__builtin_cpu_supports("avx2"))
            opttype = OPT_AVX2;
        else if (__builtin_cpu_supports("ssse3"))
            opttype = OPT_SSSE3;
#endif
    }
#endif

    // Rearrange filter taps
    if ((flags & FDAF_INTERLEAVE) && (!(flags & FDAF_INTERPOLATE)) && (cpu_vcap_get() == OPT_AVX2)) {
        for (unsigned i = 0; i < filer_taps; i++) {
            unsigned z = (((~i) & 1)) | ((i & 2) << 2) | ((i & 4)) | ((i & 8) >> 2) | (i & 0xfffffff0);
            tdata[z] = pfilter[i];
        }
    } else if (flags & FDAF_INTERPOLATE) {
        // Reorganize to poly-phase filter array
        for (unsigned k = 0; k < decim_inter; k++) {
            for (unsigned i = 0; i < f->ftaps; i++) {
                tdata[k * f->ftaps + i] = pfilter[decim_inter * i + decim_inter - k - 1];
            }
        }
    }

    f->func = conv_filter(flags);

#if 0
    if (flags & FDAF_INTERLEAVE) {
        if (flags & FDAF_INTERPOLATE) {
            f->func = conv_int16_pi;
        } else {
            switch (opttype) {
#ifndef __EMSCRIPTEN__
            case OPT_AVX2: f->func = /*conv_int16i_avx*/conv_filter(); break;
#endif
            case OPT_SSSE3: f->func = conv_int16i_sse; break;
            default: f->func = conv_int16i;
            }
        }
    } else {
        if (flags & FDAF_INTERPOLATE) {
            f->func = conv_int16_p;
        } else {
            switch (opttype) {
#ifndef __EMSCRIPTEN__
            case OPT_AVX2: f->func = conv_filter();/*conv_int16_avx;*/ break;
#endif
            case OPT_SSSE3: f->func = conv_filter();/*conv_int16_sse;*/ break;
            default: f->func = conv_filter();//conv_int16;
            }
        }
    }
#endif

    return f;
}


void filter_data_free(filter_data_t* o)
{
    free(o);
}

int16_t* filter_data_ptr(filter_data_t* o)
{
    return (int16_t*)o->user_data;
}

int16_t* filter_data_ptr2(filter_data_t* o)
{
    return (int16_t*)o->user_data;
}

void filter_data_process(filter_data_t* o, int16_t* out)
{
    o->func(o->history_data, o->filter_data, out, o->blocksz,
            o->decim_inter, o->ftaps);
    memcpy(o->history_data, o->history_data + o->blocksz,
           o->history_movebsz);
}

unsigned filter_block_size(filter_data_t* o)
{
    return o->blocksz;
}

