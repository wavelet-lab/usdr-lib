#define RTSA_U16_DISCHARGE(len) \
\
__m256i d0, d1, d2, d3; \
__m256i delta0, delta1, delta2, delta3; \
\
for(unsigned j = i; j < i + (len); ++j) \
{ \
    __m256i* ptr = (__m256i*)(rtsa_data->pwr + j * rtsa_depth); \
    unsigned n = rtsa_depth_bz; \
\
    while(n >= 128) \
    { \
        d0 = _mm256_load_si256(ptr + 0); \
        d1 = _mm256_load_si256(ptr + 1); \
        d2 = _mm256_load_si256(ptr + 2); \
        d3 = _mm256_load_si256(ptr + 3); \
\
        if(_mm256_testz_si256(d0,d0) && _mm256_testz_si256(d1,d1) && \
           _mm256_testz_si256(d2,d2) && _mm256_testz_si256(d3,d3))   \
        { \
            ptr += 4; n -= 128; \
            continue; \
        } \
\
        delta0 = _mm256_adds_epu16(_mm256_srl_epi16(d0, dch_rshift), dch_add_coef); \
        delta1 = _mm256_adds_epu16(_mm256_srl_epi16(d1, dch_rshift), dch_add_coef); \
        delta2 = _mm256_adds_epu16(_mm256_srl_epi16(d2, dch_rshift), dch_add_coef); \
        delta3 = _mm256_adds_epu16(_mm256_srl_epi16(d3, dch_rshift), dch_add_coef); \
\
        _mm256_store_si256(ptr++, _mm256_subs_epu16(d0, delta0)); \
        _mm256_store_si256(ptr++, _mm256_subs_epu16(d1, delta1)); \
        _mm256_store_si256(ptr++, _mm256_subs_epu16(d2, delta2)); \
        _mm256_store_si256(ptr++, _mm256_subs_epu16(d3, delta3)); \
\
        n -= 128; \
    } \
\
    while(n >= 64) \
    { \
        d0 = _mm256_load_si256(ptr + 0); \
        d1 = _mm256_load_si256(ptr + 1); \
\
        if(_mm256_testz_si256(d0,d0) && _mm256_testz_si256(d1,d1)) \
        { \
            ptr += 2; n -= 64; \
            continue; \
        } \
\
        delta0 = _mm256_adds_epu16(_mm256_srl_epi16(d0, dch_rshift), dch_add_coef); \
        delta1 = _mm256_adds_epu16(_mm256_srl_epi16(d1, dch_rshift), dch_add_coef); \
\
        _mm256_store_si256(ptr++, _mm256_subs_epu16(d0, delta0)); \
        _mm256_store_si256(ptr++, _mm256_subs_epu16(d1, delta1)); \
\
        n -= 64; \
    } \
\
    while(n >= 32) \
    { \
        d0 = _mm256_load_si256(ptr); \
\
        if(_mm256_testz_si256(d0,d0)) \
        { \
            ptr++; n -= 32; \
            continue; \
        } \
\
        delta0 = _mm256_adds_epu16(_mm256_srl_epi16(d0, dch_rshift), dch_add_coef); \
        _mm256_store_si256(ptr++, _mm256_subs_epu16(d0, delta0)); \
\
        n -= 32; \
    } \
    /* we definitely have n == 0 here due to rtsa_depth aligning */ \
}
