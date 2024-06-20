# Copyright (c) 2023-2024 Wavelet Lab
# SPDX-License-Identifier: MIT

set(TEST_SUIT_SRCS
    xdsp_utest_suite.c
    conv_i16_f32_utest.c
    conv_f32_i16_utest.c
    conv_i12_f32_utest.c
    conv_ci12_2cf32_utest.c
    conv_f32_i12_utest.c
    conv_2cf32_ci12_utest.c
    xfft_fftad_utest.c
    xfft_rtsa_utest.c
    fft_window_cf32_utest.c

    ../fft_window_functions.c
    ../fftad_functions.c
    ../rtsa_functions.c
    ../conv_i16_f32_2.c
    ../conv_f32_i16_2.c
    ../conv_i12_f32_2.c
    ../conv_ci12_2cf32_2.c
    ../conv_f32_i12_2.c
    ../conv_2cf32_ci12_2.c
    ../vbase.c
)

if(WVLT_ARCH_X86 OR WVLT_ARCH_X86_64)
    add_definitions("-msse4.1")
endif()

add_definitions("-O3")

add_executable(xdsp_utest_suite ${TEST_SUIT_SRCS})
target_link_libraries(xdsp_utest_suite check subunit m rt pthread)
