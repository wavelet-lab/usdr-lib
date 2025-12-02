# Copyright (c) 2023-2024 Wavelet Lab
# SPDX-License-Identifier: MIT

# Populate a CMake variable with the sources
set(xdsplib_funcs_SRCS
    ${CMAKE_CURRENT_SOURCE_DIR}/intfft.c
    ${CMAKE_CURRENT_SOURCE_DIR}/filter.c
    ${CMAKE_CURRENT_SOURCE_DIR}/nco.c
)

set(xdsplib_conv_SRCS
#   Data Convertions
    ${CMAKE_CURRENT_SOURCE_DIR}/vbase.c
    ${CMAKE_CURRENT_SOURCE_DIR}/conv.c
    ${CMAKE_CURRENT_SOURCE_DIR}/conv_i16_f32_2.c
    ${CMAKE_CURRENT_SOURCE_DIR}/conv_ci16_2cf32_2.c
    ${CMAKE_CURRENT_SOURCE_DIR}/conv_i16_4f32_2.c
    ${CMAKE_CURRENT_SOURCE_DIR}/conv_2cf32_ci16_2.c
    ${CMAKE_CURRENT_SOURCE_DIR}/conv_f32_i16_2.c
    ${CMAKE_CURRENT_SOURCE_DIR}/conv_ci16_2ci16_2.c
    ${CMAKE_CURRENT_SOURCE_DIR}/conv_2ci16_ci16_2.c
    ${CMAKE_CURRENT_SOURCE_DIR}/conv_i12_f32_2.c
    ${CMAKE_CURRENT_SOURCE_DIR}/conv_ci12_2cf32_2.c
    ${CMAKE_CURRENT_SOURCE_DIR}/conv_f32_i12_2.c
    ${CMAKE_CURRENT_SOURCE_DIR}/conv_2cf32_ci12_2.c
    ${CMAKE_CURRENT_SOURCE_DIR}/conv_filter.c
    ${CMAKE_CURRENT_SOURCE_DIR}/fftad_functions.c
    ${CMAKE_CURRENT_SOURCE_DIR}/rtsa_functions.c
    ${CMAKE_CURRENT_SOURCE_DIR}/fft_window_functions.c
    ${CMAKE_CURRENT_SOURCE_DIR}/fmquad.c
    ${CMAKE_CURRENT_SOURCE_DIR}/trig.c
    ${CMAKE_CURRENT_SOURCE_DIR}/conv_4ci16_ci16_2.c
    ${CMAKE_CURRENT_SOURCE_DIR}/conv_ci16_4ci16_2.c
    ${CMAKE_CURRENT_SOURCE_DIR}/conv_ci16_4cf32_2.c
    ${CMAKE_CURRENT_SOURCE_DIR}/conv_4cf32_ci16_2.c
    ${CMAKE_CURRENT_SOURCE_DIR}/sincos_functions.c
    ${CMAKE_CURRENT_SOURCE_DIR}/conv_ci12_4cf32_2.c
    ${CMAKE_CURRENT_SOURCE_DIR}/conv_4cf32_ci12_2.c
    ${CMAKE_CURRENT_SOURCE_DIR}/conv_i12_i16_2.c
    ${CMAKE_CURRENT_SOURCE_DIR}/conv_i16_i12_2.c
    ${CMAKE_CURRENT_SOURCE_DIR}/conv_ci12_2ci16_2.c
    ${CMAKE_CURRENT_SOURCE_DIR}/conv_ci12_4ci16_2.c
    ${CMAKE_CURRENT_SOURCE_DIR}/conv_2ci16_ci12_2.c
    ${CMAKE_CURRENT_SOURCE_DIR}/conv_4ci16_ci12_2.c
    ${CMAKE_CURRENT_SOURCE_DIR}/conv_ci16_8cf32_2.c
)

if(WVLT_ARCH_X86 OR WVLT_ARCH_X86_64)
    set(xdsplib_SRCS ${xdsplib_conv_SRCS} ${xdsplib_funcs_SRCS})
elseif(WVLT_ARCH_ARM64)
    set(xdsplib_SRCS ${xdsplib_conv_SRCS}) #Fix me!!
else()
    set(xdsplib_SRCS ${xdsplib_conv_SRCS})
endif()

#if(NOT CROSS_ARM)
#    add_definitions(-msse4.1)
#
#    if(NOT EMSCRIPTEN)
#        set_source_files_properties(conv_i16_f32_2.c PROPERTIES COMPILE_FLAGS  -mavx2)
#    endif()
#
#    set(xdsplib_SRCS ${xdsplib_conv_SRCS} ${xdsplib_funcs_SRCS})
#else(NOT CROSS_ARM)
#    set(xdsplib_SRCS ${xdsplib_conv_SRCS})
#endif(NOT CROSS_ARM)

if(EMSCRIPTEN OR WVLT_WEBUSB_BUILD)
    set(XDSPLIB_FILES ${xdsplib_SRCS} PARENT_SCOPE)
else()

    if(ENABLE_TESTS)
        add_subdirectory(utests)
    endif(ENABLE_TESTS)

    if(WVLT_ARCH_X86 OR WVLT_ARCH_X86_64)
        add_definitions("-msse4.1")
    elseif(WVLT_ARCH_ARM64)
        #add_definitions("-mfpu=neon")
    endif()

    add_library(usdr-dsp ${xdsplib_SRCS})
    target_link_libraries(usdr-dsp m pthread)
    target_compile_options(usdr-dsp PRIVATE "-Wall" "-Werror=implicit-function-declaration")
    install(TARGETS usdr-dsp LIBRARY)

    set_target_properties(usdr-dsp PROPERTIES SOVERSION ${USDR_ABI_VERSION})
    set_target_properties(usdr-dsp PROPERTIES VERSION ${USDR_LIBVER})

endif()
