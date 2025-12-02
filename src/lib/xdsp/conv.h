// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#ifndef CONV_H
#define CONV_H

#include <stdint.h>
#include <stdbool.h>
#include "usdr_port.h"
#include "vbase.h"

#define I16RND(x) (int16_t)(x)

#define HWI16_SCALE_COEF 1024.f
#define HWI16_SCALE_N2_COEF 10u
#define HWI16_CORR_COEF -178.f

union wu_u32b {uint32_t i; uint8_t b[4];};
typedef union wu_u32b wu_u32b_t;

union wu_i16b {int16_t i; uint8_t b[2];};
typedef union wu_i16b wu_i16b_t;

union wu_i16u32 {int16_t i[2]; uint32_t u;};
typedef union wu_i16u32 wu_i16u32_t;


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

#define DECLARE_TR_FUNC_1_8(conv_fn) \
void tr_##conv_fn (const void *__restrict *__restrict indata, \
                  unsigned indatabsz, \
                  void *__restrict *__restrict outdata, \
                  unsigned outdatabsz) \
{ conv_fn(*indata, indatabsz, outdata[0], outdata[1], outdata[2], outdata[3], outdata[4], outdata[5], outdata[6], outdata[7], outdatabsz); }

#define DECLARE_TR_FUNC_2_1(conv_fn) \
    void tr_##conv_fn (const void *__restrict *__restrict indata, \
                       unsigned indatabsz, \
                       void *__restrict *__restrict outdata, \
                       unsigned outdatabsz) \
   { conv_fn(indata[0], indata[1], indatabsz, outdata[0], outdatabsz); }

#define DECLARE_TR_FUNC_4_1(conv_fn) \
    void tr_##conv_fn (const void *__restrict *__restrict indata, \
                      unsigned indatabsz, \
                      void *__restrict *__restrict outdata, \
                      unsigned outdatabsz) \
   { conv_fn(indata[0], indata[1], indata[2], indata[3], indatabsz, outdata[0], outdatabsz); }


typedef void (*sincos_i16_interleaved_ctrl_function_t)(int32_t *__restrict start_phase,
                                int32_t delta_phase, int16_t gain, bool inv_sin, bool inv_cos,
                                int16_t *__restrict outdata,
                                unsigned iters);

#define DECLARE_TR_FUNC_SINCOS_I16_INTERLEAVED_CTRL(conv_fn) \
void tr_##conv_fn (int32_t *__restrict start_phase, \
                  int32_t delta_phase, \
                  int16_t gain, \
                  bool inv_sin, \
                  bool inv_cos, \
                  int16_t *__restrict outdata, \
                  unsigned iters) \
    { conv_fn(start_phase, delta_phase, gain, inv_sin, inv_cos, outdata, iters); }


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
typedef int16_t wvlt_fftwi16_complex[2];

typedef void (*fftad_init_function_t)
    (fft_acc_t* __restrict p,  unsigned fftsz);
typedef fftad_init_function_t fftad_init_hwi16_function_t;

typedef void (*fftad_add_function_t)
    (fft_acc_t* __restrict p, wvlt_fftwf_complex * __restrict d, unsigned fftsz);
typedef void (*fftad_add_hwi16_function_t)
    (fft_acc_t* __restrict p, uint16_t * __restrict d, unsigned fftsz);

typedef void (*fftad_norm_function_t)
    (fft_acc_t* __restrict p, unsigned fftsz, float scale, float corr, float* __restrict outa);
typedef fftad_norm_function_t fftad_norm_hwi16_function_t;

#define DECLARE_TR_FUNC_FFTAD_INIT(conv_fn) \
void tr_##conv_fn (fft_acc_t* __restrict p,  unsigned fftsz) \
{ conv_fn(p, fftsz); }

#define DECLARE_TR_FUNC_FFTAD_INIT_HWI16(conv_fn) DECLARE_TR_FUNC_FFTAD_INIT(conv_fn)

#define DECLARE_TR_FUNC_FFTAD_ADD(conv_fn) \
void tr_##conv_fn (fft_acc_t* __restrict p, wvlt_fftwf_complex * __restrict d, unsigned fftsz) \
{ conv_fn(p, d, fftsz); }

#define DECLARE_TR_FUNC_FFTAD_ADD_HWI16(conv_fn) \
void tr_##conv_fn (fft_acc_t* __restrict p, uint16_t * __restrict d, unsigned fftsz) \
{ conv_fn(p, d, fftsz); }

#define DECLARE_TR_FUNC_FFTAD_NORM(conv_fn) \
void tr_##conv_fn (fft_acc_t* __restrict p, unsigned fftsz, float scale, float corr, float* __restrict outa) \
{ conv_fn(p, fftsz, scale, corr, outa); }

#define DECLARE_TR_FUNC_FFTAD_NORM_HWI16(conv_fn) DECLARE_TR_FUNC_FFTAD_NORM(conv_fn)

// RTSA

struct fft_rtsa_settings
{
    int16_t upper_pwr_bound;  // upper pwr level, dB (e.g. 0)
    int16_t lower_pwr_bound;  // lower pwr levej, dB (e.g. -120)
    unsigned divs_for_dB;     // pwr granularity within 1 dB (e.g. 100)
    unsigned rtsa_depth;      // need to be calced by rtsa_calc_depth()!
    unsigned charging_frame;  // charge iterations (FFT count) for one full charge/discharge. Looks like "contrast". Should by > 0 and optimal if it is 2^n
    unsigned raise_coef;      // charge speed multiplier, 1 - the slowest. Preferred value == 24+
    unsigned decay_coef;      // discharge speed divider, 1 - the slowest. Optimal if it is 2^n. Preferred value == 1.
};
typedef struct fft_rtsa_settings fft_rtsa_settings_t;

struct rtsa_hwi16_consts
{
    float org_scale;        // original scale
    uint16_t nfft;          // log2(fft_size)
    uint16_t c0, c1;        // scaling coefs
    uint16_t ndivs_for_dB;  // log2(divs_for_dB)
    uint16_t shr0, shr1;    // shifting coefs
};
typedef struct rtsa_hwi16_consts rtsa_hwi16_consts_t;

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

struct fft_diap
{
    unsigned from, to;
};
typedef struct fft_diap fft_diap_t;

typedef void (*rtsa_update_function_t)
    (   wvlt_fftwf_complex* __restrict in, unsigned fft_size,
        fft_rtsa_data_t* __restrict rtsa_data,
        float fcale_mpy, float mine, float corr, fft_diap_t diap);

#define DECLARE_TR_FUNC_RTSA_UPDATE(conv_fn) \
void tr_##conv_fn (wvlt_fftwf_complex* __restrict in, unsigned fft_size, \
                   fft_rtsa_data_t* __restrict rtsa_data, \
                   float fcale_mpy, float mine, float corr, fft_diap_t diap) \
{ conv_fn( in, fft_size, rtsa_data, fcale_mpy, mine, corr, diap ); }


typedef void (*rtsa_update_hwi16_function_t)
    (   uint16_t* __restrict in, unsigned fft_size,
        fft_rtsa_data_t* __restrict rtsa_data,
        float fcale_mpy, float corr, fft_diap_t diap, const rtsa_hwi16_consts_t* __restrict hwi16_consts);

#define DECLARE_TR_FUNC_RTSA_UPDATE_HWI16(conv_fn) \
void tr_##conv_fn (uint16_t* __restrict in, unsigned fft_size, \
                  fft_rtsa_data_t* __restrict rtsa_data, \
                  float fcale_mpy, float corr, fft_diap_t diap, const rtsa_hwi16_consts_t* __restrict hwi16_consts) \
{ conv_fn( in, fft_size, rtsa_data, fcale_mpy, corr, diap, hwi16_consts); }


//FFT windows conv

typedef void (*fft_window_cf32_function_t)
    (wvlt_fftwf_complex* __restrict in, unsigned fftsz, float* __restrict wnd, wvlt_fftwf_complex* __restrict out);

#define DECLARE_TR_FUNC_FFT_WINDOW_CF32(conv_fn) \
void tr_##conv_fn (wvlt_fftwf_complex* __restrict in, unsigned fftsz, float* __restrict wnd, \
                   wvlt_fftwf_complex* __restrict out) \
{ conv_fn(in, fftsz, wnd, out); }

typedef void (*fft_window_ci16_cf32_function_t)
    (wvlt_fftwi16_complex* __restrict in, unsigned fftsz, float* __restrict wnd, wvlt_fftwf_complex* __restrict out);

#define DECLARE_TR_FUNC_FFT_WINDOW_CI16_CF32(conv_fn) \
void tr_##conv_fn (wvlt_fftwi16_complex* __restrict in, unsigned fftsz, float* __restrict wnd, \
                  wvlt_fftwf_complex* __restrict out) \
{ conv_fn(in, fftsz, wnd, out); }

#endif
