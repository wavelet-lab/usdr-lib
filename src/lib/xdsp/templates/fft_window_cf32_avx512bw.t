static
void TEMPLATE_FUNC_NAME(wvlt_fftwf_complex* __restrict in, unsigned fftsz, float* __restrict wnd,
                        wvlt_fftwf_complex* __restrict out)
{
    const __m512i idx0 = _mm512_set_epi32(  7,7,   6,6,   5,5,   4,4,   3,3,   2,2, 1,1, 0,0);
    const __m512i idx1 = _mm512_set_epi32(15,15, 14,14, 13,13, 12,12, 11,11, 10,10, 9,9, 8,8);

    for(unsigned i = 0; i < fftsz; i += 32)
    {
        __m512 e0 = _mm512_load_ps(&in[i +  0][0]);
        __m512 e1 = _mm512_load_ps(&in[i +  8][0]);
        __m512 e2 = _mm512_load_ps(&in[i + 16][0]);
        __m512 e3 = _mm512_load_ps(&in[i + 24][0]);

        __m512 w0 = _mm512_castsi512_ps(_mm512_stream_load_si512(&wnd[i +  0]));
        __m512 w1 = _mm512_castsi512_ps(_mm512_stream_load_si512(&wnd[i + 16]));
        //__m512 w0 = _mm512_load_ps(&wnd[i +  0]);
        //__m512 w1 = _mm512_load_ps(&wnd[i + 16]);

        __m512 dw0 = _mm512_permutexvar_ps(idx0, w0);
        __m512 dw1 = _mm512_permutexvar_ps(idx1, w0);
        __m512 dw2 = _mm512_permutexvar_ps(idx0, w1);
        __m512 dw3 = _mm512_permutexvar_ps(idx1, w1);

        __m512 r0 = _mm512_mul_ps(e0, dw0);
        __m512 r1 = _mm512_mul_ps(e1, dw1);
        __m512 r2 = _mm512_mul_ps(e2, dw2);
        __m512 r3 = _mm512_mul_ps(e3, dw3);

        _mm512_store_ps(&out[i +  0][0], r0);
        _mm512_store_ps(&out[i +  8][0], r1);
        _mm512_store_ps(&out[i + 16][0], r2);
        _mm512_store_ps(&out[i + 24][0], r3);
    }
}

#undef TEMPLATE_FUNC_NAME
