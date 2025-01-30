#define RTSA_GATHER(dst, src, reg, n) \
    switch(n) \
    { \
    case 0 : dst = vld1_lane_u16(rtsa_data->pwr + (i + n + reg) * rtsa_depth + (unsigned)vgetq_lane_f32(src, 0), dst, 0); break; \
    case 1 : dst = vld1_lane_u16(rtsa_data->pwr + (i + n + reg) * rtsa_depth + (unsigned)vgetq_lane_f32(src, 1), dst, 1); break; \
    case 2 : dst = vld1_lane_u16(rtsa_data->pwr + (i + n + reg) * rtsa_depth + (unsigned)vgetq_lane_f32(src, 2), dst, 2); break; \
    case 3 : dst = vld1_lane_u16(rtsa_data->pwr + (i + n + reg) * rtsa_depth + (unsigned)vgetq_lane_f32(src, 3), dst, 3); break; \
    }

#define RTSA_GATHER_U16(dst, src, reg, n) \
    switch(n) \
    { \
    case 0 : dst = vld1q_lane_u16(rtsa_data->pwr + (i + n + reg) * rtsa_depth + (unsigned)vgetq_lane_u16(src, 0), dst, 0); break; \
    case 1 : dst = vld1q_lane_u16(rtsa_data->pwr + (i + n + reg) * rtsa_depth + (unsigned)vgetq_lane_u16(src, 1), dst, 1); break; \
    case 2 : dst = vld1q_lane_u16(rtsa_data->pwr + (i + n + reg) * rtsa_depth + (unsigned)vgetq_lane_u16(src, 2), dst, 2); break; \
    case 3 : dst = vld1q_lane_u16(rtsa_data->pwr + (i + n + reg) * rtsa_depth + (unsigned)vgetq_lane_u16(src, 3), dst, 3); break; \
    case 4 : dst = vld1q_lane_u16(rtsa_data->pwr + (i + n + reg) * rtsa_depth + (unsigned)vgetq_lane_u16(src, 4), dst, 4); break; \
    case 5 : dst = vld1q_lane_u16(rtsa_data->pwr + (i + n + reg) * rtsa_depth + (unsigned)vgetq_lane_u16(src, 5), dst, 5); break; \
    case 6 : dst = vld1q_lane_u16(rtsa_data->pwr + (i + n + reg) * rtsa_depth + (unsigned)vgetq_lane_u16(src, 6), dst, 6); break; \
    case 7 : dst = vld1q_lane_u16(rtsa_data->pwr + (i + n + reg) * rtsa_depth + (unsigned)vgetq_lane_u16(src, 7), dst, 7); break; \
    }

#define RTSA_SCATTER(dst, src, reg, n) \
    switch(n) \
    { \
    case 0 : vst1_lane_u16(rtsa_data->pwr + (i + n + reg) * rtsa_depth + (unsigned)vgetq_lane_f32(dst, 0), src, 0); break; \
    case 1 : vst1_lane_u16(rtsa_data->pwr + (i + n + reg) * rtsa_depth + (unsigned)vgetq_lane_f32(dst, 1), src, 1); break; \
    case 2 : vst1_lane_u16(rtsa_data->pwr + (i + n + reg) * rtsa_depth + (unsigned)vgetq_lane_f32(dst, 2), src, 2); break; \
    case 3 : vst1_lane_u16(rtsa_data->pwr + (i + n + reg) * rtsa_depth + (unsigned)vgetq_lane_f32(dst, 3), src, 3); break; \
    }

#define RTSA_SCATTER_U16(dst, src, reg, n) \
    switch(n) \
    { \
    case 0 : vst1q_lane_u16(rtsa_data->pwr + (i + n + reg) * rtsa_depth + (unsigned)vgetq_lane_u16(dst, 0), src, 0); break; \
    case 1 : vst1q_lane_u16(rtsa_data->pwr + (i + n + reg) * rtsa_depth + (unsigned)vgetq_lane_u16(dst, 1), src, 1); break; \
    case 2 : vst1q_lane_u16(rtsa_data->pwr + (i + n + reg) * rtsa_depth + (unsigned)vgetq_lane_u16(dst, 2), src, 2); break; \
    case 3 : vst1q_lane_u16(rtsa_data->pwr + (i + n + reg) * rtsa_depth + (unsigned)vgetq_lane_u16(dst, 3), src, 3); break; \
    case 4 : vst1q_lane_u16(rtsa_data->pwr + (i + n + reg) * rtsa_depth + (unsigned)vgetq_lane_u16(dst, 4), src, 4); break; \
    case 5 : vst1q_lane_u16(rtsa_data->pwr + (i + n + reg) * rtsa_depth + (unsigned)vgetq_lane_u16(dst, 5), src, 5); break; \
    case 6 : vst1q_lane_u16(rtsa_data->pwr + (i + n + reg) * rtsa_depth + (unsigned)vgetq_lane_u16(dst, 6), src, 6); break; \
    case 7 : vst1q_lane_u16(rtsa_data->pwr + (i + n + reg) * rtsa_depth + (unsigned)vgetq_lane_u16(dst, 7), src, 7); break; \
    }

#define RTSA_U16_DISCHARGE(len) \
\
uint16x8_t d0, d1, d2, d3; \
\
for(unsigned j = i; j < i + (len); ++j) \
{ \
    uint16_t* ptr = rtsa_data->pwr + j * rtsa_depth; \
    unsigned n = rtsa_depth_bz; \
\
    while(n >= 64) \
    { \
        d0 = vld1q_u16(ptr + 0); \
        d1 = vld1q_u16(ptr + 8); \
        d2 = vld1q_u16(ptr + 16); \
        d3 = vld1q_u16(ptr + 24); \
        vst1q_u16(ptr + 0,  vqsubq_u16(vqsubq_u16(d0, dch_add_coef), vshlq_u16(d0, decay_shr))); \
        vst1q_u16(ptr + 8,  vqsubq_u16(vqsubq_u16(d1, dch_add_coef), vshlq_u16(d1, decay_shr))); \
        vst1q_u16(ptr + 16, vqsubq_u16(vqsubq_u16(d2, dch_add_coef), vshlq_u16(d2, decay_shr))); \
        vst1q_u16(ptr + 24, vqsubq_u16(vqsubq_u16(d3, dch_add_coef), vshlq_u16(d3, decay_shr))); \
\
        n -= 64; \
        ptr += 32; \
    } \
\
    while(n >= 32) \
    { \
        d0 = vld1q_u16(ptr + 0); \
        d1 = vld1q_u16(ptr + 8); \
        vst1q_u16(ptr + 0, vqsubq_u16(vqsubq_u16(d0, dch_add_coef), vshlq_u16(d0, decay_shr))); \
        vst1q_u16(ptr + 8, vqsubq_u16(vqsubq_u16(d1, dch_add_coef), vshlq_u16(d1, decay_shr))); \
\
        n -= 32; \
        ptr += 16; \
    } \
\
    while(n >= 16) \
    { \
        d0 = vld1q_u16(ptr + 0); \
        vst1q_u16(ptr + 0, vqsubq_u16(vqsubq_u16(d0, dch_add_coef), vshlq_u16(d0, decay_shr))); \
\
        n -= 16; \
        ptr += 8; \
    } \
    /* we definitely have n == 0 here due to rtsa_depth aligning */ \
}
