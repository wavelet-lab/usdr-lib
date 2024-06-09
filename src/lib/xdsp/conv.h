// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef CONV_H
#define CONV_H

#include <stdint.h>
#include <stdbool.h>
#include "vbase.h"

#define I16RND(x) x > 0 ? (int16_t)(x + 0.5f) : (int16_t)(x - 0.5f)

typedef void (*conv_function_t)(const void *__restrict *__restrict indata,
                                unsigned indatabsz,
                                void *__restrict *__restrict outdata,
                                unsigned outdatabsz);

typedef unsigned (*size_function_t)(unsigned inbytes, bool reverse);

typedef void (*filter_function_t)(const int16_t *__restrict data,
                                  const int16_t *__restrict conv,
                                  int16_t *__restrict out,
                                  unsigned count,
                                  unsigned decim_bits,
                                  unsigned flen);

#define DECLARE_TR_FUNC_1_1(conv_fn) \
    void tr_##conv_fn (const void *__restrict *__restrict indata, \
                       unsigned indatabsz, \
                       void *__restrict *__restrict outdata, \
                       unsigned outdatabsz) \
   { conv_fn(*indata, indatabsz, *outdata, outdatabsz); }


#define DECLARE_TR_FUNC_1_2(conv_fn) \
    void tr_##conv_fn (const void *__restrict *__restrict indata, \
                       unsigned indatabsz, \
                       void *__restrict *__restrict outdata, \
                       unsigned outdatabsz) \
   { conv_fn(*indata, indatabsz, outdata[0], outdata[1], outdatabsz); }

#define DECLARE_TR_FUNC_1_4(conv_fn) \
    void tr_##conv_fn (const void *__restrict *__restrict indata, \
                       unsigned indatabsz, \
                       void *__restrict *__restrict outdata, \
                       unsigned outdatabsz) \
   { conv_fn(*indata, indatabsz, outdata[0], outdata[1], outdata[2], outdata[3], outdatabsz); }

#define DECLARE_TR_FUNC_2_1(conv_fn) \
    void tr_##conv_fn (const void *__restrict *__restrict indata, \
                       unsigned indatabsz, \
                       void *__restrict *__restrict outdata, \
                       unsigned outdatabsz) \
   { conv_fn(indata[0], indata[1], indatabsz, outdata[0], outdatabsz); }

struct transform_info {
    conv_function_t cfunc;
    size_function_t sfunc;
};
typedef struct transform_info transform_info_t;

transform_info_t get_transform_fn(const char* from,
                                 const char* to,
                                 unsigned inveccnt,
                                 unsigned outveccnt);

bool is_transform_dummy(conv_function_t t);

#define DECLARE_TR_FUNC_FILTER(conv_fn) \
void tr_##conv_fn (const int16_t *__restrict data, \
                   const int16_t *__restrict conv, \
                   int16_t *__restrict out, \
                   unsigned count, \
                   unsigned decim_bits, \
                   unsigned flen) \
{ conv_fn(data, conv, out, count, decim_bits, flen); }


struct fft_accumulate_data {
    float* f_mant;
    int32_t* f_pwr;
    float mine;
};
typedef struct fft_accumulate_data fft_acc_t;

typedef float wvlt_fftwf_complex[2];

typedef void (*fftad_init_function_t)
    (fft_acc_t* __restrict p,  unsigned fftsz);
typedef void (*fftad_add_function_t)
    (fft_acc_t* __restrict p, wvlt_fftwf_complex * __restrict d, unsigned fftsz);
typedef void (*fftad_norm_function_t)
    (fft_acc_t* __restrict p, unsigned fftsz, float scale, float corr, double* __restrict outa);


#define DECLARE_TR_FUNC_FFTAD_INIT(conv_fn) \
void tr_##conv_fn (fft_acc_t* __restrict p,  unsigned fftsz) \
{ conv_fn(p, fftsz); }

#define DECLARE_TR_FUNC_FFTAD_ADD(conv_fn) \
void tr_##conv_fn (fft_acc_t* __restrict p, wvlt_fftwf_complex * __restrict d, unsigned fftsz) \
{ conv_fn(p, d, fftsz); }

#define DECLARE_TR_FUNC_FFTAD_NORM(conv_fn) \
void tr_##conv_fn (fft_acc_t* __restrict p, unsigned fftsz, float scale, float corr, double* __restrict outa) \
{ conv_fn(p, fftsz, scale, corr, outa); }


// RTSA

struct fft_rtsa_settings
{
    int16_t upper_pwr_bound;  // upper pwr level, dB (e.g. 0)
    int16_t lower_pwr_bound;  // lower pwr levej, dB (e.g. -120)
    unsigned divs_for_dB;     // pwr granularity within 1 dB (e.g. 100)
    unsigned rtsa_depth;      // need to be calced by rtsa_calc_depth()!
    unsigned averaging;       // averaging count for 1 cycle. Optimal if it is 2^n
    unsigned raise_coef;      // charge speed multiplier, 1 - the slowest. Preferred value == 24+
    unsigned decay_coef;      // discharge speed divider, 1 - the slowest. Optimal if it is 2^n. Preferred value == 1.
};
typedef struct fft_rtsa_settings fft_rtsa_settings_t;

#undef RTSA_FP32

#ifdef RTSA_FP32
#define MAX_RTSA_PWR 1.0f
typedef float rtsa_pwr_t;
#else
#define MAX_RTSA_PWR 65535
typedef uint16_t rtsa_pwr_t;
#endif

struct fft_rtsa_data
{
    fft_rtsa_settings_t settings;
    rtsa_pwr_t* pwr;
};
typedef struct fft_rtsa_data fft_rtsa_data_t;

typedef void (*rtsa_update_function_t)
    (   wvlt_fftwf_complex* __restrict in, unsigned fft_size,
        fft_rtsa_data_t* __restrict rtsa_data,
        float fcale_mpy, float mine, float corr);

#define DECLARE_TR_FUNC_RTSA_UPDATE(conv_fn) \
void tr_##conv_fn (wvlt_fftwf_complex* __restrict in, unsigned fft_size, \
                   fft_rtsa_data_t* __restrict rtsa_data, \
                   float fcale_mpy, float mine, float corr) \
{ conv_fn( in, fft_size, rtsa_data, fcale_mpy, mine, corr ); }

#endif
