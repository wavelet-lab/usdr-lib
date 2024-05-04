static
void TEMPLATE_FUNC_NAME(const void *__restrict indata,
                        unsigned indatabsz,
                        void *__restrict outdata_p,
                        unsigned outdatabsz)
{
#define inscale      CONV_SCALE
#define SCALE2(x)    ((x)/65536)

  size_t i = indatabsz;
  if ((outdatabsz / 2) < i)
    i = (outdatabsz / 2);

  const __m128i* vp = (const __m128i* )indata;
  float* outdata = (float*)outdata_p;
  __m128i d0, d1, d2, d3;
  __m128 f0, f1, f2, f3;
  __m128 scale = _mm_set_ps(SCALE2(inscale), SCALE2(inscale), SCALE2(inscale), SCALE2(inscale));
  __m128i t0;
  __m128i t1;

  if (i >= 32) {
      t0 = _mm_loadu_si128(vp++);
      t1 = _mm_loadu_si128(vp++);

      for (; i >= 64; i -= 32) {
          d0 = _mm_unpacklo_epi16(_mm_set1_epi16(0), t0);
          d1 = _mm_unpackhi_epi16(_mm_set1_epi16(0), t0);
          d2 = _mm_unpacklo_epi16(_mm_set1_epi16(0), t1);
          d3 = _mm_unpackhi_epi16(_mm_set1_epi16(0), t1);

          t0 = _mm_load_si128(vp++);
          t1 = _mm_load_si128(vp++);

          f0 = _mm_cvtepi32_ps(d0);    // Latency 3
          f1 = _mm_cvtepi32_ps(d1);    // Latency 3
          f2 = _mm_cvtepi32_ps(d2);    // Latency 3
          f3 = _mm_cvtepi32_ps(d3);    // Latency 3

          f0 = _mm_mul_ps(f0, scale);  // Latency 5
          _mm_storeu_ps(outdata, f0); outdata+=4;
          f1 = _mm_mul_ps(f1, scale);
          _mm_storeu_ps(outdata, f1); outdata+=4;
          f2 = _mm_mul_ps(f2, scale);  // Latency 5
          _mm_storeu_ps(outdata, f2); outdata+=4;
          f3 = _mm_mul_ps(f3, scale);
          _mm_storeu_ps(outdata, f3); outdata+=4;
      }

      i -= 32;

      // Last portion of 32 + bytes % 32
      d0 = _mm_unpacklo_epi16(_mm_set1_epi16(0), t0);
      d1 = _mm_unpackhi_epi16(_mm_set1_epi16(0), t0);
      d2 = _mm_unpacklo_epi16(_mm_set1_epi16(0), t1);
      d3 = _mm_unpackhi_epi16(_mm_set1_epi16(0), t1);

      if (i >= 16) {
          t0 = _mm_loadu_si128(vp++);
      }

      f0 = _mm_cvtepi32_ps(d0);    // Latency 3
      f1 = _mm_cvtepi32_ps(d1);    // Latency 3
      f2 = _mm_cvtepi32_ps(d2);    // Latency 3
      f3 = _mm_cvtepi32_ps(d3);    // Latency 3

      f0 = _mm_mul_ps(f0, scale);  // Latency 5
      _mm_storeu_ps(outdata, f0); outdata+=4;
      f1 = _mm_mul_ps(f1, scale);
      _mm_storeu_ps(outdata, f1); outdata+=4;
      f2 = _mm_mul_ps(f2, scale);  // Latency 5
      _mm_storeu_ps(outdata, f2); outdata+=4;
      f3 = _mm_mul_ps(f3, scale);
      _mm_storeu_ps(outdata, f3); outdata+=4;

      if (i == 0)
          return;
  } else if (i >= 16) {
      t0 = _mm_loadu_si128(vp++);
  }

  if (i >= 16) {
      i -= 16;

      // Last portion of 32 + bytes % 32
      d0 = _mm_unpacklo_epi16(_mm_set1_epi16(0), t0);
      d1 = _mm_unpackhi_epi16(_mm_set1_epi16(0), t0);

      f0 = _mm_cvtepi32_ps(d0);    // Latency 3
      f1 = _mm_cvtepi32_ps(d1);    // Latency 3

      f0 = _mm_mul_ps(f0, scale);  // Latency 5
      _mm_storeu_ps(outdata, f0); outdata+=4;
      f1 = _mm_mul_ps(f1, scale);
      _mm_storeu_ps(outdata, f1); outdata+=4;
  }

  if (i > 0) {
      const int16_t *ldw = (const int16_t *)vp;
      for (; i >= 2; i -= 2) {
          *(outdata++) = *(ldw++) * inscale;
      }
  }
}

#undef TEMPLATE_FUNC_NAME
