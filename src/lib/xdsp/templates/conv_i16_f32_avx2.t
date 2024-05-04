static
void TEMPLATE_FUNC_NAME(const int16_t *__restrict indata,
                        unsigned indatabsz,
                        float *__restrict outdata,
                        unsigned outdatabsz)
{
#define inscale      CONV_SCALE
#define SCALE2(x)    ((x)/65536)

  size_t i = indatabsz;
  if ((outdatabsz / 2) < i)
    i = (outdatabsz / 2);

  const __m256i* vp = (const __m256i* )indata;
  __m256i d0, d1, d2, d3;
  __m256 f0, f1, f2, f3;
  __m256 p0, p1, p2, p3;
  __m256 scale = _mm256_set_ps(SCALE2(inscale), SCALE2(inscale), SCALE2(inscale), SCALE2(inscale), SCALE2(inscale), SCALE2(inscale), SCALE2(inscale), SCALE2(inscale));
  __m256i t0;
  __m256i t1;

  if (i >= 64) {
      t0 = _mm256_loadu_si256(vp++); // Latency 7
      t1 = _mm256_loadu_si256(vp++); // Latency 7

      for (; i >= 128; i -= 64) {
          d0 = _mm256_unpacklo_epi16(_mm256_set1_epi16(0), t0); // 0 1 2 3 8 9 A B
          d1 = _mm256_unpackhi_epi16(_mm256_set1_epi16(0), t0); // 4 5 6 7 C D E F
          d2 = _mm256_unpacklo_epi16(_mm256_set1_epi16(0), t1);
          d3 = _mm256_unpackhi_epi16(_mm256_set1_epi16(0), t1);

          t0 = _mm256_load_si256(vp++);   // Latency 7
          t1 = _mm256_load_si256(vp++);   // Latency 7

          f0 = _mm256_cvtepi32_ps(d0);    // Latency 3-4
          f1 = _mm256_cvtepi32_ps(d1);    // Latency 3-4
          f2 = _mm256_cvtepi32_ps(d2);    // Latency 3-4
          f3 = _mm256_cvtepi32_ps(d3);    // Latency 3-4

          f0 = _mm256_mul_ps(f0, scale);  // Latency 4-5
          f1 = _mm256_mul_ps(f1, scale);  // Latency 4-5
          f2 = _mm256_mul_ps(f2, scale);  // Latency 4-5
          f3 = _mm256_mul_ps(f3, scale);  // Latency 4-5

          p0 = _mm256_permute2f128_ps(f0, f1, 0x20);  // Latency 3
          p1 = _mm256_permute2f128_ps(f0, f1, 0x31);  // Latency 3
          p2 = _mm256_permute2f128_ps(f2, f3, 0x20);  // Latency 3
          p3 = _mm256_permute2f128_ps(f2, f3, 0x31);  // Latency 3

          _mm256_storeu_ps(outdata, p0); outdata += 8;
          _mm256_storeu_ps(outdata, p1); outdata += 8;
          _mm256_storeu_ps(outdata, p2); outdata += 8;
          _mm256_storeu_ps(outdata, p3); outdata += 8;
      }

      i -= 64;

      // Last portion of 64 + bytes % 64
      d0 = _mm256_unpacklo_epi16(_mm256_set1_epi16(0), t0);
      d1 = _mm256_unpackhi_epi16(_mm256_set1_epi16(0), t0);
      d2 = _mm256_unpacklo_epi16(_mm256_set1_epi16(0), t1);
      d3 = _mm256_unpackhi_epi16(_mm256_set1_epi16(0), t1);

      if (i >= 32) {
          t0 = _mm256_loadu_si256(vp++);
      }

      f0 = _mm256_cvtepi32_ps(d0);    // Latency 3
      f1 = _mm256_cvtepi32_ps(d1);    // Latency 3
      f2 = _mm256_cvtepi32_ps(d2);    // Latency 3
      f3 = _mm256_cvtepi32_ps(d3);    // Latency 3

      f0 = _mm256_mul_ps(f0, scale);  // Latency 5
      f1 = _mm256_mul_ps(f1, scale);
      f2 = _mm256_mul_ps(f2, scale);  // Latency 5
      f3 = _mm256_mul_ps(f3, scale);

      p0 = _mm256_permute2f128_ps(f0, f1, 0x20); 
      p1 = _mm256_permute2f128_ps(f0, f1, 0x31);
      p2 = _mm256_permute2f128_ps(f2, f3, 0x20);
      p3 = _mm256_permute2f128_ps(f2, f3, 0x31);

      _mm256_storeu_ps(outdata, p0); outdata += 8;
      _mm256_storeu_ps(outdata, p1); outdata += 8;
      _mm256_storeu_ps(outdata, p2); outdata += 8;
      _mm256_storeu_ps(outdata, p3); outdata += 8;

      if (i == 0)
          return;
  } else if (i >= 32) {
      t0 = _mm256_loadu_si256(vp++);
  }

  if (i >= 32) {
      i -= 32;

      // Last portion of 64 + bytes % 64
      d0 = _mm256_unpacklo_epi16(_mm256_set1_epi16(0), t0);
      d1 = _mm256_unpackhi_epi16(_mm256_set1_epi16(0), t0);

      f0 = _mm256_cvtepi32_ps(d0);    // Latency 3
      f1 = _mm256_cvtepi32_ps(d1);    // Latency 3

      f0 = _mm256_mul_ps(f0, scale);  // Latency 5
      f1 = _mm256_mul_ps(f1, scale);

      p0 = _mm256_permute2f128_ps(f0, f1, 0x20); 
      p1 = _mm256_permute2f128_ps(f0, f1, 0x31);

      _mm256_storeu_ps(outdata, p0); outdata+=8;
      _mm256_storeu_ps(outdata, p1); outdata+=8;
  }

  if (i > 0) {
      const int16_t *ldw = (const int16_t *)vp;
      for (; i >= 2; i -= 2) {
          *(outdata++) = *(ldw++) * inscale;
      }
  }
}

#undef TEMPLATE_FUNC_NAME
