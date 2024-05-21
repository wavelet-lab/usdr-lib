// Copyright (c) 2023-2024 Wavelet Lab
// SPDX-License-Identifier: MIT

#include <stdlib.h>
#include <check.h>
#include <stdio.h>
#include "../vbase.h"

Suite * conv_i16_f32_suite(void);
Suite * fftad_suite(void);
Suite * rtsa_suite(void);
Suite * fft_window_cf32_suite(void);

int main(int argc, char** argv)
{
    char buffer[100];
    cpu_vcap_str(buffer, sizeof(buffer), cpu_vcap_obtain(0));
    fprintf(stderr, "Running up to %s CPU features\n", buffer);

    int number_failed;
    SRunner *sr;
#if 1
    sr = srunner_create(conv_i16_f32_suite());
    srunner_add_suite(sr, fftad_suite());
    srunner_add_suite(sr, rtsa_suite());
    srunner_add_suite(sr, fft_window_cf32_suite());
#else
    sr = srunner_create(fft_window_cf32_suite());
#endif
    srunner_set_fork_status (sr, CK_NOFORK);
    srunner_run_all(sr, (argc > 1) ? CK_VERBOSE : CK_NORMAL);
    number_failed = srunner_ntests_failed(sr);

    srunner_free(sr);

    return (number_failed == 0) ? EXIT_SUCCESS : EXIT_FAILURE;
}
