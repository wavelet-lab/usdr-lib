#ifndef UNWRAP_CNT
#define UNWRAP_CNT 2
#endif

#if UNWRAP_CNT > 2
#error Maximum spported UNWRAP_CNT is 2!
#endif

static
void TEMPLATE_FUNC_NAME(wvlt_fftwi16_complex* __restrict in, unsigned fftsz, float* __restrict wnd,
                        wvlt_fftwf_complex* __restrict out)
{
    const __m512i idx0 = _mm512_set_epi32(  7,7,   6,6,   5,5,   4,4,   3,3,   2,2, 1,1, 0,0);
    const __m512i idx1 = _mm512_set_epi32(15,15, 14,14, 13,13, 12,12, 11,11, 10,10, 9,9, 8,8);

    for(unsigned i = 0; i < fftsz; i += UNWRAP_CNT * 32)
    {
        __m512i i0 = _mm512_load_si512(&in[i +  0][0]);
        __m512i i1 = _mm512_load_si512(&in[i + 16][0]);
#if UNWRAP_CNT > 1
        __m512i i2 = _mm512_load_si512(&in[i + 32][0]);
        __m512i i3 = _mm512_load_si512(&in[i + 48][0]);
#endif

        __m512 w0 = _mm512_castsi512_ps(_mm512_stream_load_si512(&wnd[i +  0]));
        __m512 w1 = _mm512_castsi512_ps(_mm512_stream_load_si512(&wnd[i + 16]));
        //__m512 w0 = _mm512_load_ps(&wnd[i +  0]);
        //__m512 w1 = _mm512_load_ps(&wnd[i + 16]);
#if UNWRAP_CNT > 1
        __m512 w2 = _mm512_castsi512_ps(_mm512_stream_load_si512(&wnd[i + 32]));
        __m512 w3 = _mm512_castsi512_ps(_mm512_stream_load_si512(&wnd[i + 48]));
        //__m512 w2 = _mm512_load_ps(&wnd[i + 32]);
        //__m512 w3 = _mm512_load_ps(&wnd[i + 48]);
#endif

        __m512 dw0 = _mm512_permutexvar_ps(idx0, w0);
        __m512 dw1 = _mm512_permutexvar_ps(idx1, w0);
        __m512 dw2 = _mm512_permutexvar_ps(idx0, w1);
        __m512 dw3 = _mm512_permutexvar_ps(idx1, w1);
#if UNWRAP_CNT > 1
        __m512 dw4 = _mm512_permutexvar_ps(idx0, w2);
        __m512 dw5 = _mm512_permutexvar_ps(idx1, w2);
        __m512 dw6 = _mm512_permutexvar_ps(idx0, w3);
        __m512 dw7 = _mm512_permutexvar_ps(idx1, w3);
#endif

        __m512i d0 = _mm512_cvtepi16_epi32(_mm512_castsi512_si256(i0));
        __m512i d1 = _mm512_cvtepi16_epi32(_mm512_extracti64x4_epi64(i0, 1));
        __m512i d2 = _mm512_cvtepi16_epi32(_mm512_castsi512_si256(i1));
        __m512i d3 = _mm512_cvtepi16_epi32(_mm512_extracti64x4_epi64(i1, 1));
#if UNWRAP_CNT > 1
        __m512i d4 = _mm512_cvtepi16_epi32(_mm512_castsi512_si256(i2));
        __m512i d5 = _mm512_cvtepi16_epi32(_mm512_extracti64x4_epi64(i2, 1));
        __m512i d6 = _mm512_cvtepi16_epi32(_mm512_castsi512_si256(i3));
        __m512i d7 = _mm512_cvtepi16_epi32(_mm512_extracti64x4_epi64(i3, 1));
#endif

        __m512 e0 = _mm512_cvtepi32_ps(d0);
        __m512 e1 = _mm512_cvtepi32_ps(d1);
        __m512 e2 = _mm512_cvtepi32_ps(d2);
        __m512 e3 = _mm512_cvtepi32_ps(d3);
#if UNWRAP_CNT > 1
        __m512 e4 = _mm512_cvtepi32_ps(d4);
        __m512 e5 = _mm512_cvtepi32_ps(d5);
        __m512 e6 = _mm512_cvtepi32_ps(d6);
        __m512 e7 = _mm512_cvtepi32_ps(d7);
#endif

        __m512 r0 = _mm512_mul_ps(e0, dw0);
        __m512 r1 = _mm512_mul_ps(e1, dw1);
        __m512 r2 = _mm512_mul_ps(e2, dw2);
        __m512 r3 = _mm512_mul_ps(e3, dw3);
#if UNWRAP_CNT > 1
        __m512 r4 = _mm512_mul_ps(e4, dw4);
        __m512 r5 = _mm512_mul_ps(e5, dw5);
        __m512 r6 = _mm512_mul_ps(e6, dw6);
        __m512 r7 = _mm512_mul_ps(e7, dw7);
#endif

        _mm512_store_ps(&out[i +  0][0], r0);
        _mm512_store_ps(&out[i +  8][0], r1);
        _mm512_store_ps(&out[i + 16][0], r2);
        _mm512_store_ps(&out[i + 24][0], r3);
#if UNWRAP_CNT > 1
        _mm512_store_ps(&out[i + 32][0], r4);
        _mm512_store_ps(&out[i + 40][0], r5);
        _mm512_store_ps(&out[i + 48][0], r6);
        _mm512_store_ps(&out[i + 56][0], r7);
#endif
    }
}

#undef TEMPLATE_FUNC_NAME
