static
void TEMPLATE_FUNC_NAME(const void *__restrict indata,
                        unsigned indatabsz,
                        void *__restrict outa_p,
                        void *__restrict outb_p,
                        unsigned outdatabsz)
{
#define inscale      CONV_SCALE
#define SCALE2(x)    ((x)/65536)
  const __m128i* vp = (const __m128i* )indata;

  float* outa = (float*)outa_p;
  float* outb = (float*)outb_p;

  size_t i = indatabsz;
  if ((outdatabsz / 2) < i) {
    i = (outdatabsz / 2);
    }

    __m128i d0, d1, d2, d3;
    __m128 f0, f1, f2, f3;
    __m128 z0, z1, z2, z3;
    __m128 scale = _mm_set_ps(SCALE2(inscale), SCALE2(inscale), SCALE2(inscale), SCALE2(inscale));
    __m128i ands = _mm_set_epi32(0xffff0000, 0xffff0000, 0xffff0000, 0xffff0000);
    __m128i t0;
    __m128i t1;

  if (i >= 32) {
      t0 = _mm_loadu_si128(vp++);
      t1 = _mm_loadu_si128(vp++);

      for (; i >= 64; i -= 32) {
          d0 = _mm_and_si128(t0, ands); // B3..B0
          d1 = _mm_slli_si128(t0, 2);
          d2 = _mm_and_si128(t1, ands); // B7..B4
          d3 = _mm_slli_si128(t1, 2);

          t0 = _mm_load_si128(vp++);
          t1 = _mm_load_si128(vp++);

          d1 = _mm_and_si128(d1, ands); // A3..A0
          f1 = _mm_cvtepi32_ps(d0);    // Latency 3
          d3 = _mm_and_si128(d3, ands); // A4..A4

          f0 = _mm_cvtepi32_ps(d1);    // Latency 3

          f2 = _mm_cvtepi32_ps(d3);    // Latency 3
          f3 = _mm_cvtepi32_ps(d2);    // Latency 3

          z0 = _mm_mul_ps(f0, scale);  // Latency 5
          _mm_storeu_ps(outa, z0); outa+=4;
          z1 = _mm_mul_ps(f1, scale);
          _mm_storeu_ps(outb, z1); outb+=4;
          z2 = _mm_mul_ps(f2, scale);  // Latency 5
          _mm_storeu_ps(outa, z2); outa+=4;
          z3 = _mm_mul_ps(f3, scale);
          _mm_storeu_ps(outb, z3); outb+=4;
      }

      i -= 32;

        d0 = _mm_and_si128(t0, ands); // B3..B0
        d1 = _mm_slli_si128(t0, 2);
        d2 = _mm_and_si128(t1, ands); // B3..B0
        d3 = _mm_slli_si128(t1, 2);

        if (i >= 16) {
           t0 = _mm_loadu_si128(vp++);
        }

        d1 = _mm_and_si128(d1, ands); // A3..A0
        f1 = _mm_cvtepi32_ps(d0);    // Latency 3
        d3 = _mm_and_si128(d3, ands); // A4..A4

        f0 = _mm_cvtepi32_ps(d1);    // Latency 3

        f2 = _mm_cvtepi32_ps(d3);    // Latency 3
        f3 = _mm_cvtepi32_ps(d2);    // Latency 3

        z0 = _mm_mul_ps(f0, scale);  // Latency 5
        _mm_storeu_ps(outa, z0); outa+=4;
        z1 = _mm_mul_ps(f1, scale);
        _mm_storeu_ps(outb, z1); outb+=4;
        z2 = _mm_mul_ps(f2, scale);  // Latency 5
        _mm_storeu_ps(outa, z2); outa+=4;
        z3 = _mm_mul_ps(f3, scale);
        _mm_storeu_ps(outb, z3); outb+=4;

      if (i == 0)
          return;
  } else if (i >= 16) {
      t0 = _mm_loadu_si128(vp++);
      i -= 16;

        // Last portion of 32 + bytes % 32
        d0 = _mm_and_si128(t0, ands); // B3..B0
        d1 = _mm_slli_si128(t0, 2);
        d1 = _mm_and_si128(d1, ands);

        f0 = _mm_cvtepi32_ps(d1);    // Latency 3
        f1 = _mm_cvtepi32_ps(d0);    // Latency 3

        f0 = _mm_mul_ps(f0, scale);  // Latency 5
        f1 = _mm_mul_ps(f1, scale);

        _mm_storeu_ps(outa, f0); outa += 4;
        _mm_storeu_ps(outb, f1); outb += 4;
  }

  if (i > 0) {
      t0 = _mm_loadl_epi64((const __m128i* )vp); //ldw += 4;
      d0 = _mm_and_si128(t0, ands); // B3..B0
      d1 = _mm_slli_si128(t0, 2);
      f1 = _mm_cvtepi32_ps(d0);     // Latency 3
      d1 = _mm_and_si128(d1, ands); // A3..A0
      f0 = _mm_cvtepi32_ps(d1);     // Latency 3
      z0 = _mm_mul_ps(f0, scale);   // Latency 5
      _mm_storel_epi64(( __m128i* )outa, _mm_castps_si128(z0)); //outa+=2;
      z1 = _mm_mul_ps(f1, scale);
      _mm_storel_epi64(( __m128i* )outb, _mm_castps_si128(z1)); //outb+=2;
  }
}

#undef TEMPLATE_FUNC_NAME
