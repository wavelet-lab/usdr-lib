#ifndef UNWRAP_CNT
#define UNWRAP_CNT 4
#endif

#if UNWRAP_CNT > 4
#error Maximum spported UNWRAP_CNT is 4!
#endif

static
void TEMPLATE_FUNC_NAME(const int16_t *__restrict indata,
                        unsigned indatabsz,
                        float *__restrict outdata,
                        unsigned outdatabsz)
{
  size_t i = indatabsz;
  if ((outdatabsz / 2) < i)
    i = (outdatabsz / 2);

  const __m512i* vp = (const __m512i* )indata;
  __m512 scale = _mm512_set1_ps(CONV_SCALE);
  __m512i t0, t1;
  __m512 f0, f1, f2, f3;
#if UNWRAP_CNT > 1
  __m512i t2, t3;
  __m512 f4, f5, f6, f7;
#if UNWRAP_CNT > 2
  __m512i t4, t5;
  __m512 f8, f9, fA, fB;
#if UNWRAP_CNT > 3
  __m512i t6, t7;
  __m512 fC, fD, fE, fF;
#endif
#endif
#endif

#define CONVERT_I16_F32_BLOCK(reg0, reg1, f0, f1, f2, f3) \
    {   \
        __m512i d0 = _mm512_cvtepi16_epi32(_mm512_castsi512_si256(reg0));         \
        __m512i d1 = _mm512_cvtepi16_epi32(_mm512_extracti64x4_epi64(reg0, 1));    \
        __m512i d2 = _mm512_cvtepi16_epi32(_mm512_castsi512_si256(reg1));         \
        __m512i d3 = _mm512_cvtepi16_epi32(_mm512_extracti64x4_epi64(reg1, 1));    \
        \
        f0 = _mm512_cvtepi32_ps(d0); \
        f1 = _mm512_cvtepi32_ps(d1); \
        f2 = _mm512_cvtepi32_ps(d2); \
        f3 = _mm512_cvtepi32_ps(d3); \
        \
        f0 = _mm512_mul_ps(f0, scale);  \
        f1 = _mm512_mul_ps(f1, scale);  \
        f2 = _mm512_mul_ps(f2, scale);  \
        f3 = _mm512_mul_ps(f3, scale);  \
        \
        _mm512_store_ps(outdata, f0); \
        _mm512_store_ps(outdata, f1); \
    }
// CONVERT_I16_F32_BLOCK end


  while(i >= UNWRAP_CNT * 128)
  {
    t0 = _mm512_load_si512(vp++);
    t1 = _mm512_load_si512(vp++);
#if UNWRAP_CNT > 1
    t2 = _mm512_load_si512(vp++);
    t3 = _mm512_load_si512(vp++);
#if UNWRAP_CNT > 2
    t4 = _mm512_load_si512(vp++);
    t5 = _mm512_load_si512(vp++);
#if UNWRAP_CNT > 3
    t6 = _mm512_load_si512(vp++);
    t7 = _mm512_load_si512(vp++);
#endif
#endif
#endif

    CONVERT_I16_F32_BLOCK(t0, t1, f0, f1, f2, f3);
#if UNWRAP_CNT > 1
    CONVERT_I16_F32_BLOCK(t2, t3, f4, f5, f6, f7);
#if UNWRAP_CNT > 2
    CONVERT_I16_F32_BLOCK(t4, t5, f8, f9, fA, fB);
#if UNWRAP_CNT > 3
    CONVERT_I16_F32_BLOCK(t6, t7, fC, fD, fE, fF);
#endif
#endif
#endif

    _mm512_store_ps(outdata + 0x00, f0);
    _mm512_store_ps(outdata + 0x10, f1);
    _mm512_store_ps(outdata + 0x20, f2);
    _mm512_store_ps(outdata + 0x30, f3);
#if UNWRAP_CNT > 1
    _mm512_store_ps(outdata + 0x40, f4);
    _mm512_store_ps(outdata + 0x50, f5);
    _mm512_store_ps(outdata + 0x60, f6);
    _mm512_store_ps(outdata + 0x70, f7);
#if UNWRAP_CNT > 2
    _mm512_store_ps(outdata + 0x80, f8);
    _mm512_store_ps(outdata + 0x90, f9);
    _mm512_store_ps(outdata + 0xa0, fA);
    _mm512_store_ps(outdata + 0xb0, fB);
#if UNWRAP_CNT > 3
    _mm512_store_ps(outdata + 0xc0, fC);
    _mm512_store_ps(outdata + 0xd0, fD);
    _mm512_store_ps(outdata + 0xe0, fE);
    _mm512_store_ps(outdata + 0xf0, fF);
#endif
#endif
#endif

    outdata += UNWRAP_CNT * 64;
    i -= UNWRAP_CNT * 128;
  }

  while(i >= 128)
  {
    t0 = _mm512_load_si512(vp++);
    t1 = _mm512_load_si512(vp++);

    CONVERT_I16_F32_BLOCK(t0, t1, f0, f1, f2, f3);

    _mm512_store_ps(outdata +   0, f0);
    _mm512_store_ps(outdata +  16, f1);
    _mm512_store_ps(outdata +  32, f2);
    _mm512_store_ps(outdata +  48, f3);

    outdata += 64;
    i -= 128;
  }

#undef CONVERT_I16_F32_BLOCK

  const int16_t *ldw = (const int16_t *)vp;
  for (; i >= 2; i -= 2) {
      *(outdata++) = *(ldw++) * CONV_SCALE;
  }
}

#undef TEMPLATE_FUNC_NAME
