# Copyright (c) 2023-2024 Wavelet Lab
# SPDX-License-Identifier: MIT

set(TEST_SUIT_SRCS
    xdsp_utest_suite.c
    conv_i16_f32_utest.c
    conv_i12_f32_utest.c
    conv_ci12_2cf32_utest.c
    xfft_fftad_utest.c
    xfft_rtsa_utest.c

    ../fftad_functions.c
    ../rtsa_functions.c
    ../conv_i16_f32_2.c
    ../conv_i12_f32_2.c
    ../conv_ci12_2cf32_2.c
    ../vbase.c
)

if(WVLT_ARCH_X86 OR WVLT_ARCH_X86_64)
    add_definitions("-mavx2 -mfma")
endif()

add_definitions("-O3")

add_executable(xdsp_utest_suite ${TEST_SUIT_SRCS})
target_link_libraries(xdsp_utest_suite check subunit m rt pthread)
