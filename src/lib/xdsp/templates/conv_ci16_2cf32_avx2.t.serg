static
void TEMPLATE_FUNC_NAME(const int16_t *__restrict indata,
                        unsigned indatabsz,
                        float *__restrict outa,
                        float *__restrict outb,
                        unsigned outdatabsz)
{
#define inscale      CONV_SCALE
#define SCALE2(x)    ((x)/65536)
    const __m256i* vp = (const __m256i* )indata;
    size_t i = indatabsz;
    if ((outdatabsz / 2) < i) {
        i = (outdatabsz / 2);
    }

    __m256i d0, d1, d2, d3;
    __m256 f0, f1, f2, f3;
    __m256 z0, z1, z2, z3;
    __m256 scale = _mm256_set_ps(SCALE2(inscale), SCALE2(inscale), SCALE2(inscale), SCALE2(inscale), SCALE2(inscale), SCALE2(inscale), SCALE2(inscale), SCALE2(inscale));
    __m256i ands = _mm256_set_epi32(0xffff0000, 0xffff0000, 0xffff0000, 0xffff0000, 0xffff0000, 0xffff0000, 0xffff0000, 0xffff0000);
    __m256i t0;
    __m256i t1;

  if (i >= 64) {
      t0 = _mm256_loadu_si256(vp++);
      t1 = _mm256_loadu_si256(vp++);

      for (; i >= 128; i -= 64) {
          d0 = _mm256_and_si256(t0, ands); // B7..B0
          d1 = _mm256_slli_si256(t0, 2);
          d2 = _mm256_and_si256(t1, ands); // B15..B8
          d3 = _mm256_slli_si256(t1, 2);

          t0 = _mm256_load_si256(vp++);
          t1 = _mm256_load_si256(vp++);

          d1 = _mm256_and_si256(d1, ands); // A7..A0
          f1 = _mm256_cvtepi32_ps(d0);     // Latency 3
          d3 = _mm256_and_si256(d3, ands); // A15..A8

          f0 = _mm256_cvtepi32_ps(d1);    // Latency 3

          f2 = _mm256_cvtepi32_ps(d3);    // Latency 3
          f3 = _mm256_cvtepi32_ps(d2);    // Latency 3

          z0 = _mm256_mul_ps(f0, scale);  // Latency 5
          _mm256_storeu_ps(outa, z0); outa += 8;
          z1 = _mm256_mul_ps(f1, scale);
          _mm256_storeu_ps(outb, z1); outb += 8;
          z2 = _mm256_mul_ps(f2, scale);  // Latency 5
          _mm256_storeu_ps(outa, z2); outa += 8;
          z3 = _mm256_mul_ps(f3, scale);
          _mm256_storeu_ps(outb, z3); outb += 8;
      }

      i -= 64;

        d0 = _mm256_and_si256(t0, ands); // B7..B0
        d1 = _mm256_slli_si256(t0, 2);
        d2 = _mm256_and_si256(t1, ands); // B15..B8
        d3 = _mm256_slli_si256(t1, 2);

        if (i >= 32) {
           t0 = _mm256_loadu_si256(vp++);
        }

        d1 = _mm256_and_si256(d1, ands); // A7..A0
        f1 = _mm256_cvtepi32_ps(d0);     // Latency 3
        d3 = _mm256_and_si256(d3, ands); // A15..A8

        f0 = _mm256_cvtepi32_ps(d1);    // Latency 3

        f2 = _mm256_cvtepi32_ps(d3);    // Latency 3
        f3 = _mm256_cvtepi32_ps(d2);    // Latency 3

        z0 = _mm256_mul_ps(f0, scale);  // Latency 5
        _mm256_storeu_ps(outa, z0); outa += 8;
        z1 = _mm256_mul_ps(f1, scale);
        _mm256_storeu_ps(outb, z1); outb += 8;
        z2 = _mm256_mul_ps(f2, scale);  // Latency 5
        _mm256_storeu_ps(outa, z2); outa += 8;
        z3 = _mm256_mul_ps(f3, scale);
        _mm256_storeu_ps(outb, z3); outb += 8;

      if (i == 0)
          return;
  } else if (i >= 32) {
      t0 = _mm256_loadu_si256(vp++);
      i -= 32;

        // Last portion of 64 + bytes % 64
        d0 = _mm256_and_si256(t0, ands); // B7..B0
        d1 = _mm256_slli_si256(t0, 2);
        d1 = _mm256_and_si256(d1, ands);

        f0 = _mm256_cvtepi32_ps(d1);    // Latency 3
        f1 = _mm256_cvtepi32_ps(d0);    // Latency 3

        f0 = _mm256_mul_ps(f0, scale);  // Latency 5
        f1 = _mm256_mul_ps(f1, scale);

        _mm256_storeu_ps(outa, f0); outa += 8;
        _mm256_storeu_ps(outb, f1); outb += 8;
  }

  // Tail {IA QA IB QB} 64bit operations
  for (unsigned k = 0; i > 7; i -= 8, k++) {
      __m128i ld0, ld1;
      __m128 lf0, lf1;
      __m128 lz0, lz1;
      __m128 lscale = _mm_set_ps(SCALE2(inscale), SCALE2(inscale), SCALE2(inscale), SCALE2(inscale));
      __m128i lands = _mm_set_epi32(0xffff0000, 0xffff0000, 0xffff0000, 0xffff0000);
      __m128i lt0;

      lt0 = _mm_loadl_epi64((const __m128i* )( (uint64_t*)vp + k )); //ldw += 4;
      ld0 = _mm_and_si128(lt0, lands); // B3..B0
      ld1 = _mm_slli_si128(lt0, 2);
      lf1 = _mm_cvtepi32_ps(ld0);      // Latency 3
      ld1 = _mm_and_si128(ld1, lands); // A3..A0
      lf0 = _mm_cvtepi32_ps(ld1);      // Latency 3
      lz0 = _mm_mul_ps(lf0, lscale);   // Latency 5
      _mm_storel_epi64(( __m128i* )outa, _mm_castps_si128(lz0)); outa += 2;
      lz1 = _mm_mul_ps(lf1, lscale);
      _mm_storel_epi64(( __m128i* )outb, _mm_castps_si128(lz1)); outb += 2;
  }
}

#undef TEMPLATE_FUNC_NAME
