#define RTSA_SHIFT4(n) \
    delta0 = vsraq_n_u16(dch_add_coef, d0, n); \
    delta1 = vsraq_n_u16(dch_add_coef, d1, n); \
    delta2 = vsraq_n_u16(dch_add_coef, d2, n); \
    delta3 = vsraq_n_u16(dch_add_coef, d3, n);

#define RTSA_CHARGE_SHIFT2(n) \
    cdelta0 = vsraq_n_u16(ch_add_coef, pwr0, n); \
    cdelta1 = vsraq_n_u16(ch_add_coef, pwr1, n);

#define RTSA_SHIFT2(n) \
    delta0 = vsraq_n_u16(dch_add_coef, d0, n); \
    delta1 = vsraq_n_u16(dch_add_coef, d1, n);

#define RTSA_SHIFT1(n) \
    delta0 = vsraq_n_u16(dch_add_coef, d0, n);

#define RTSA_SH_SWITCH(shft, n) \
    switch(n) \
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
uint16x8_t delta0, delta1, delta2, delta3; \
uint16x8_t delta_norm0, delta_norm1, delta_norm2, delta_norm3; \
uint16x8_t res0, res1, res2, res3; \
\
for(unsigned j = i; j < i + (len); ++j) \
{ \
    uint16_t* ptr = rtsa_data->pwr + j * rtsa_depth; \
    unsigned n = rtsa_depth_bz; \
\
    while(n >= 64) \
    { \
        d0 = vld1q_u16(ptr + 0);  \
        d1 = vld1q_u16(ptr + 8);  \
        d2 = vld1q_u16(ptr + 16); \
        d3 = vld1q_u16(ptr + 24); \
\
        if(!vmaxvq_u16(d0) && !vmaxvq_u16(d1) && !vmaxvq_u16(d2) && !vmaxvq_u16(d3)) \
        { \
            n -= 64; ptr += 32; \
            continue; \
        } \
\
        RTSA_SH_SWITCH(RTSA_SHIFT4, decay_rate_pw2) \
\
        delta_norm0 = vminq_u16(delta0, d0); \
        delta_norm1 = vminq_u16(delta1, d1); \
        delta_norm2 = vminq_u16(delta2, d2); \
        delta_norm3 = vminq_u16(delta3, d3); \
\
        res0 = vsubq_u16(d0, delta_norm0); \
        res1 = vsubq_u16(d1, delta_norm1); \
        res2 = vsubq_u16(d2, delta_norm2); \
        res3 = vsubq_u16(d3, delta_norm3); \
\
        vst1q_u16(ptr + 0 , res0); \
        vst1q_u16(ptr + 8 , res1); \
        vst1q_u16(ptr + 16, res2); \
        vst1q_u16(ptr + 24, res3); \
\
        n -= 64; \
        ptr += 32; \
    } \
\
    while(n >= 32) \
    { \
        d0 = vld1q_u16(ptr + 0); \
        d1 = vld1q_u16(ptr + 8); \
\
        if(!vmaxvq_u16(d0) && !vmaxvq_u16(d1)) \
        { \
            n -= 32; ptr += 16; \
            continue; \
        } \
\
        RTSA_SH_SWITCH(RTSA_SHIFT2, decay_rate_pw2) \
\
        delta_norm0 = vminq_u16(delta0, d0); \
        delta_norm1 = vminq_u16(delta1, d1); \
\
        res0 = vsubq_u16(d0, delta_norm0); \
        res1 = vsubq_u16(d1, delta_norm1); \
\
        vst1q_u16(ptr + 0 , res0); \
        vst1q_u16(ptr + 8 , res1); \
\
        n -= 32; \
        ptr += 16; \
    } \
\
    while(n >= 16) \
    { \
        d0 = vld1q_u16(ptr + 0); \
\
        if(!vmaxvq_u16(d0)) \
        { \
            n -= 16; ptr += 8; \
            continue; \
        } \
\
        RTSA_SH_SWITCH(RTSA_SHIFT1, decay_rate_pw2) \
        delta_norm0 = vminq_u16(delta0, d0); \
        res0 = vsubq_u16(d0, delta_norm0); \
        vst1q_u16(ptr + 0 , res0); \
\
        n -= 16; \
        ptr += 8; \
    } \
    /* we definitely have n == 0 here due to rtsa_depth aligning */ \
}
