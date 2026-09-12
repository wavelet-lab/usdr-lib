const __m256i permmask1 = _mm256_set_epi32(7, 3, 5, 1, 6, 2, 4, 0);

#define STORE_CI12_4CI16_BLOCK(reg0, reg1, reg2, reg3) \
{ \
    __m256i rs0, rs1, rs2, rs3; \
    CONVERT_I12_I16_BLOCK(reg0, rs0); \
    CONVERT_I12_I16_BLOCK(reg1, rs1); \
    CONVERT_I12_I16_BLOCK(reg2, rs2); \
    CONVERT_I12_I16_BLOCK(reg3, rs3); \
    \
    rs0 = _mm256_permutevar8x32_epi32(rs0, permmask1); \
    rs1 = _mm256_permutevar8x32_epi32(rs1, permmask1); \
    rs2 = _mm256_permutevar8x32_epi32(rs2, permmask1); \
    rs3 = _mm256_permutevar8x32_epi32(rs3, permmask1); \
    \
    __m256i z0 = _mm256_castpd_si256(_mm256_shuffle_pd(_mm256_castsi256_pd(rs0), _mm256_castsi256_pd(rs1), 0b0000)); \
    __m256i z1 = _mm256_castpd_si256(_mm256_shuffle_pd(_mm256_castsi256_pd(rs0), _mm256_castsi256_pd(rs1), 0b1111)); \
    __m256i z2 = _mm256_castpd_si256(_mm256_shuffle_pd(_mm256_castsi256_pd(rs2), _mm256_castsi256_pd(rs3), 0b0000)); \
    __m256i z3 = _mm256_castpd_si256(_mm256_shuffle_pd(_mm256_castsi256_pd(rs2), _mm256_castsi256_pd(rs3), 0b1111)); \
    \
    __m256i i0 = _mm256_permute2x128_si256(z0, z2, 0b00100000); \
    __m256i i1 = _mm256_permute2x128_si256(z0, z2, 0b00110001); \
    __m256i i2 = _mm256_permute2x128_si256(z1, z3, 0b00100000); \
    __m256i i3 = _mm256_permute2x128_si256(z1, z3, 0b00110001); \
    \
    _mm256_storeu_si256((__m256i*)outdata_0, i0); \
    _mm256_storeu_si256((__m256i*)outdata_1, i1); \
    _mm256_storeu_si256((__m256i*)outdata_2, i2); \
    _mm256_storeu_si256((__m256i*)outdata_3, i3); \
    \
    outdata_0 += 16; \
    outdata_1 += 16; \
    outdata_2 += 16; \
    outdata_3 += 16; \
}
