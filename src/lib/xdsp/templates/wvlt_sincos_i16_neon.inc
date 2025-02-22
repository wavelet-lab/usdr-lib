const int16x8_t max_s16 = vdupq_n_s16(32767);
const int16x8_t min_s16 = vdupq_n_s16(-32768);

#define WVLT_SINCOS(rph, rs, rc) \
    { \
        int16x8_t ph2    = vqrdmulhq_s16(rph, rph); \
        int16x8_t phx1   = vqrdmulhq_n_s16(rph, 18705); \
        int16x8_t phx3_c = vqrdmulhq_n_s16(rph, -21166); \
        int16x8_t phx5_c = vqrdmulhq_n_s16(rph, 2611); \
        int16x8_t phx7_c = vqrdmulhq_n_s16(rph, -152); \
        \
        int16x8_t ph4  = vqrdmulhq_s16(ph2, ph2); \
        int16x8_t phx3 = vqrdmulhq_s16(ph2, phx3_c); \
        int16x8_t phy2 = vqrdmulhq_n_s16(ph2, -7656); \
        \
        int16x8_t phs0 = vaddq_s16(rph, phx1); \
        int16x8_t phc0 = vsubq_s16(max_s16, ph2); \
        int16x8_t phs1 = vaddq_s16(phs0, phx3); \
        int16x8_t phc1 = vaddq_s16(phc0, phy2); \
        \
        int16x8_t ph6   = vqrdmulhq_s16(ph4, ph2); \
        int16x8_t phx5  = vqrdmulhq_s16(ph4, phx5_c); \
        int16x8_t phy48 = vqrdmulhq_n_s16(ph4, 30); \
        int16x8_t phy4  = vqrdmulhq_n_s16(ph4, 8311); \
        \
        int16x8_t phy6 = vqrdmulhq_n_s16(ph6, -683); \
        int16x8_t phx7 = vqrdmulhq_s16(ph6, phx7_c); \
        int16x8_t phy8 = vqrdmulhq_s16(ph4, phy48); \
        \
        int16x8_t phs2 = vaddq_s16(phs1, phx5); \
        int16x8_t phc2 = vaddq_s16(phc1, phy4); \
        \
        rs = vaddq_s16(phs2, phx7); \
        int16x8_t phc3 = vaddq_s16(phc2, phy6); \
        rc = vaddq_s16(phc3, phy8); \
    }
//  WVLT_SINCOS
