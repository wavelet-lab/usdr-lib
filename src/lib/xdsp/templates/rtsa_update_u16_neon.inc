#define RTSA_SHIFT4(n) \
    delta0 = vsraq_n_u16(dch_add_coef, d0, n); \
    delta1 = vsraq_n_u16(dch_add_coef, d1, n); \
    delta2 = vsraq_n_u16(dch_add_coef, d2, n); \
    delta3 = vsraq_n_u16(dch_add_coef, d3, n);

#define RTSA_SHIFT2(n) \
    delta0 = vsraq_n_u16(dch_add_coef, d0, n); \
    delta1 = vsraq_n_u16(dch_add_coef, d1, n);

#define RTSA_SHIFT1(n) \
    delta0 = vsraq_n_u16(dch_add_coef, d0, n);

#define RTSA_SH_SWITCH(shft) \
    switch(decay_rate_pw2) \
    { \
    case  1: shft(1); break; \
    case  2: shft(2); break; \
    case  3: shft(3); break; \
    case  4: shft(4); break; \
    case  5: shft(5); break; \
    case  6: shft(6); break; \
    case  7: shft(7); break; \
    case  8: shft(8); break; \
    case  9: shft(9); break; \
    case 10: shft(10); break; \
    case 11: shft(11); break; \
    case 12: shft(12); break; \
    case 13: shft(13); break; \
    case 14: shft(14); break; \
    case 15: shft(15); break; \
    default: shft(16); \
    }

#define RTSA_GATHER(dst, src, reg, n) \
    switch(n) \
    { \
    case 0 : dst = vld1_lane_u16(rtsa_data->pwr + (i + n + reg) * rtsa_depth + (unsigned)vgetq_lane_f32(src, 0), dst, 0); break; \
    case 1 : dst = vld1_lane_u16(rtsa_data->pwr + (i + n + reg) * rtsa_depth + (unsigned)vgetq_lane_f32(src, 1), dst, 1); break; \
    case 2 : dst = vld1_lane_u16(rtsa_data->pwr + (i + n + reg) * rtsa_depth + (unsigned)vgetq_lane_f32(src, 2), dst, 2); break; \
    case 3 : dst = vld1_lane_u16(rtsa_data->pwr + (i + n + reg) * rtsa_depth + (unsigned)vgetq_lane_f32(src, 3), dst, 3); break; \
    }

#define RTSA_SCATTER(dst, src, reg, n) \
    switch(n) \
    { \
    case 0 : vst1_lane_u16(rtsa_data->pwr + (i + n + reg) * rtsa_depth + (unsigned)vgetq_lane_f32(dst, 0), src, 0); break; \
    case 1 : vst1_lane_u16(rtsa_data->pwr + (i + n + reg) * rtsa_depth + (unsigned)vgetq_lane_f32(dst, 1), src, 1); break; \
    case 2 : vst1_lane_u16(rtsa_data->pwr + (i + n + reg) * rtsa_depth + (unsigned)vgetq_lane_f32(dst, 2), src, 2); break; \
    case 3 : vst1_lane_u16(rtsa_data->pwr + (i + n + reg) * rtsa_depth + (unsigned)vgetq_lane_f32(dst, 3), src, 3); break; \
    }
