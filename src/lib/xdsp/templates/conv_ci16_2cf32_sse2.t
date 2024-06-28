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

        for (; i >= 64; i -= 32)
        {

/*
*  |              (1)              |              (0)              |
*  +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
*  | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 | 8 |
*  +---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+---+
*  |  f7   |  f6   |  f5   |  f4   |  f3   |  f2   |  f1   |  f0   |
*
*  t0:
*  |  f5   |  f7   |  f4   |  f6   |  f1   |  f3   |  f0   |  f2   |
*  d0:
*  |  f5   |  00   |  f4   |  00   |  f1   |  00   |  f0   |  00   |
*  d1:
*  |  f7   |  00   |  f6   |  00   |  f3   |  00   |  f2   |  00   |
*/

            t0 = _mm_shufflelo_epi16(t0, _MM_SHUFFLE(1, 3, 0, 2));
            t0 = _mm_shufflehi_epi16(t0, _MM_SHUFFLE(1, 3, 0, 2));
            d0 = _mm_and_si128(t0, ands);
            d1 = _mm_and_si128(_mm_slli_si128(t0, 2), ands);

            t1 = _mm_shufflelo_epi16(t1, _MM_SHUFFLE(1, 3, 0, 2));
            t1 = _mm_shufflehi_epi16(t1, _MM_SHUFFLE(1, 3, 0, 2));
            d2 = _mm_and_si128(t1, ands);
            d3 = _mm_and_si128(_mm_slli_si128(t1, 2), ands);

            t0 = _mm_load_si128(vp++);
            t1 = _mm_load_si128(vp++);

            f0 = _mm_cvtepi32_ps(d0);    // Latency 3
            f1 = _mm_cvtepi32_ps(d1);    // Latency 3
            f2 = _mm_cvtepi32_ps(d2);    // Latency 3
            f3 = _mm_cvtepi32_ps(d3);    // Latency 3

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

        t0 = _mm_shufflelo_epi16(t0, _MM_SHUFFLE(1, 3, 0, 2));
        t0 = _mm_shufflehi_epi16(t0, _MM_SHUFFLE(1, 3, 0, 2));
        d0 = _mm_and_si128(t0, ands);
        d1 = _mm_and_si128(_mm_slli_si128(t0, 2), ands);

        t1 = _mm_shufflelo_epi16(t1, _MM_SHUFFLE(1, 3, 0, 2));
        t1 = _mm_shufflehi_epi16(t1, _MM_SHUFFLE(1, 3, 0, 2));
        d2 = _mm_and_si128(t1, ands);
        d3 = _mm_and_si128(_mm_slli_si128(t1, 2), ands);

        if (i >= 16) {
           t0 = _mm_loadu_si128(vp++);
        }

        f0 = _mm_cvtepi32_ps(d0);    // Latency 3
        f1 = _mm_cvtepi32_ps(d1);    // Latency 3
        f2 = _mm_cvtepi32_ps(d2);    // Latency 3
        f3 = _mm_cvtepi32_ps(d3);    // Latency 3

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

        t0 = _mm_shufflelo_epi16(t0, _MM_SHUFFLE(1, 3, 0, 2));
        t0 = _mm_shufflehi_epi16(t0, _MM_SHUFFLE(1, 3, 0, 2));
        d0 = _mm_and_si128(t0, ands);
        d1 = _mm_and_si128(_mm_slli_si128(t0, 2), ands);

        f0 = _mm_cvtepi32_ps(d0);    // Latency 3
        f1 = _mm_cvtepi32_ps(d1);    // Latency 3

        f0 = _mm_mul_ps(f0, scale);  // Latency 5
        f1 = _mm_mul_ps(f1, scale);

        _mm_storeu_ps(outa, f0); outa += 4;
        _mm_storeu_ps(outb, f1); outb += 4;
    }

    const uint64_t *ld = (const uint64_t *)vp;

    for (; i >= 8; i -= 8)
    {
        uint64_t v = *(ld++);
        int16_t a = (int16_t)(v);
        int16_t b = (int16_t)(v>>16);
        int16_t c = (int16_t)(v>>32);
        int16_t d = (int16_t)(v>>48);

        *(outa++) = a;
        *(outa++) = b;
        *(outb++) = c;
        *(outb++) = d;
    }

    // do nothing with leftover
}

#undef TEMPLATE_FUNC_NAME
