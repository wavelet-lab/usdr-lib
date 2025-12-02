// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include <stdlib.h>
#include <check.h>
#include <stdio.h>
#include "../vbase.h"

Suite * conv_i16_f32_suite(void);
Suite * conv_f32_i16_suite(void);
Suite * conv_ci16_2cf32_suite(void);
Suite * conv_2cf32_ci16_suite(void);
Suite * conv_ci16_2ci16_suite(void);
Suite * conv_2ci16_ci16_suite(void);
Suite * fftad_suite(void);
Suite * rtsa_suite(void);
Suite * fft_window_cf32_suite(void);
Suite * fft_window_ci16_cf32_suite(void);
Suite * conv_i12_f32_suite(void);
Suite * conv_ci12_2cf32_suite(void);
Suite * conv_f32_i12_suite(void);
Suite * conv_2cf32_ci12_suite(void);
Suite * conv_4ci16_ci16_suite(void);
Suite * wvlt_sincos_i16_suite(void);
Suite * conv_ci16_4cf32_suite(void);
Suite * conv_4cf32_ci16_suite(void);
Suite * conv_ci12_4cf32_suite(void);
Suite * conv_4cf32_ci12_suite(void);
Suite * conv_ci16_4ci16_suite(void);
Suite * conv_i12_i16_suite(void);
Suite * conv_i16_i12_suite(void);
Suite * conv_ci12_2ci16_suite(void);
Suite * conv_ci12_4ci16_suite(void);
Suite * conv_2ci16_ci12_suite(void);
Suite * conv_4ci16_ci12_suite(void);

int main(int argc, char** argv)
{
    char buffer[100];
    cpu_vcap_str(buffer, sizeof(buffer), cpu_vcap_obtain(0));
    fprintf(stderr, "Running up to %s CPU features\n", buffer);

    int number_failed;
    SRunner *sr;
#if 0
    sr = srunner_create(  fftad_suite());
    srunner_add_suite(sr, rtsa_suite());
    srunner_add_suite(sr, fft_window_cf32_suite());
    srunner_add_suite(sr, wvlt_sincos_i16_suite());
    //
    srunner_add_suite(sr, conv_i16_f32_suite());
    srunner_add_suite(sr, conv_ci16_2cf32_suite());
    srunner_add_suite(sr, conv_ci16_4cf32_suite());
    //
    srunner_add_suite(sr, conv_f32_i16_suite());
    srunner_add_suite(sr, conv_2cf32_ci16_suite());
    srunner_add_suite(sr, conv_4cf32_ci16_suite());
    //
    srunner_add_suite(sr, conv_ci16_2ci16_suite());
    srunner_add_suite(sr, conv_ci16_4ci16_suite());
    srunner_add_suite(sr, conv_2ci16_ci16_suite());
    srunner_add_suite(sr, conv_4ci16_ci16_suite());
    //
    srunner_add_suite(sr, conv_i16_i12_suite());
    srunner_add_suite(sr, conv_2ci16_ci12_suite());
    srunner_add_suite(sr, conv_4ci16_ci12_suite());
    //
    srunner_add_suite(sr, conv_i12_i16_suite());
    srunner_add_suite(sr, conv_ci12_2ci16_suite());
    srunner_add_suite(sr, conv_ci12_4ci16_suite());
    //
    srunner_add_suite(sr, conv_f32_i12_suite());
    srunner_add_suite(sr, conv_2cf32_ci12_suite());
    srunner_add_suite(sr, conv_4cf32_ci12_suite());
    //
    srunner_add_suite(sr, conv_i12_f32_suite());
    srunner_add_suite(sr, conv_ci12_2cf32_suite());
    srunner_add_suite(sr, conv_ci12_4cf32_suite());
    //
#else
    //sr = srunner_create(conv_i16_f32_suite());
    sr = srunner_create(fft_window_ci16_cf32_suite());
    //srunner_add_suite(sr, fft_window_cf32_suite());
    //srunner_add_suite(sr, conv_f32_i12_suite());
    //srunner_add_suite(sr, conv_2cf32_ci12_suite());
#endif
    srunner_set_fork_status (sr, CK_NOFORK);
    srunner_run_all(sr, (argc > 1) ? CK_VERBOSE : CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);

    srunner_free(sr);

    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
