static
void TEMPLATE_FUNC_NAME(const int16_t *__restrict indata,
                        unsigned indatabsz,
                        float *__restrict outdata,
                        unsigned outdatabsz)
{
  size_t i = indatabsz;
  if ((outdatabsz / 2) < i)
    i = (outdatabsz / 2);

  const __m256i* vp = (const __m256i* )indata;
  __m256 scale = _mm256_set1_ps(CONV_SCALE);
  __m256i t0, t1, t2;

#define CONVERT_I16_F32_BLOCK(reg) \
    {   \
        __m256i d0 = _mm256_cvtepi16_epi32(_mm256_castsi256_si128(reg));         \
        __m256i d1 = _mm256_cvtepi16_epi32(_mm256_extracti128_si256(reg, 1));    \
        \
        __m256 f0 = _mm256_cvtepi32_ps(d0); \
        __m256 f1 = _mm256_cvtepi32_ps(d1); \
        \
        f0 = _mm256_mul_ps(f0, scale);  \
        f1 = _mm256_mul_ps(f1, scale);  \
        \
        _mm256_store_ps(outdata, f0); outdata += 8;    \
        _mm256_store_ps(outdata, f1); outdata += 8;    \
    }
// CONVERT_I16_F32_BLOCK end

  for(; i >= 96; i -= 96)
  {
     t0 = _mm256_load_si256(vp++);
     t1 = _mm256_load_si256(vp++);
     t2 = _mm256_load_si256(vp++);

     CONVERT_I16_F32_BLOCK(t0);
     CONVERT_I16_F32_BLOCK(t1);
     CONVERT_I16_F32_BLOCK(t2);
  }

  for(; i >= 64; i -= 64)
  {
     t0 = _mm256_load_si256(vp++);
     t1 = _mm256_load_si256(vp++);

     CONVERT_I16_F32_BLOCK(t0);
     CONVERT_I16_F32_BLOCK(t1);
  }

  for(; i >= 32; i -= 32)
  {
     t0 = _mm256_load_si256(vp++);
     CONVERT_I16_F32_BLOCK(t0);
  }

#undef CONVERT_I16_F32_BLOCK

  const int16_t *ldw = (const int16_t *)vp;
  for (; i >= 2; i -= 2) {
      *(outdata++) = *(ldw++) * CONV_SCALE;
  }
}

#undef TEMPLATE_FUNC_NAME
