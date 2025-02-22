static
void TEMPLATE_FUNC_NAME(int32_t *__restrict start_phase,
                        int32_t delta_phase,
                        bool inv_sin,
                        bool inv_cos,
                        int16_t *__restrict outdata,
                        unsigned iters)
{
    unsigned i = iters;
    int32_t phase = *start_phase;
    const int16_t sign_sin = inv_sin ? -1 : 1;
    const int16_t sign_cos = inv_cos ? -1 : 1;

    const int32x4_t phase_cut_mask = vdupq_n_s32(WVLT_SINCOS_I16_TWO_PI - 1);

    const uint16x8_t vsign_sin = vdupq_n_u16(inv_sin ? 0xffff : 0);
    const uint16x8_t vsign_cos = vdupq_n_u16(inv_cos ? 0xffff : 0);

    const int32x4_t vdeltap0 = {0 * delta_phase, 1 * delta_phase, 2 * delta_phase, 3 * delta_phase};
    const int32x4_t vdeltap1 = {4 * delta_phase, 5 * delta_phase, 6 * delta_phase, 7 * delta_phase};

    const int32x4_t v1_2pi = vdupq_n_s32(WVLT_SINCOS_I16_HALF_PI);
    const int32x4_t v3_2pi = vdupq_n_s32(3 * WVLT_SINCOS_I16_HALF_PI);

#include "wvlt_sincos_i16_neon.inc"

    while(i >= 8)
    {
        // add delta & phase %= WVLT_SINCOS_I16_TWO_PI
        int32x4_t vphase  = vdupq_n_s32(phase);
        int32x4_t vphase0 = vandq_s32(vaddq_s32(vdeltap0, vphase), phase_cut_mask);
        int32x4_t vphase1 = vandq_s32(vaddq_s32(vdeltap1, vphase), phase_cut_mask);

        // take phase low i16 and pack
        int16x8_t reg_phase = vcombine_s16(vmovn_s32(vphase0), vmovn_s32(vphase1));
        int16x8_t reg_sin, reg_cos;
        WVLT_SINCOS(reg_phase, reg_sin, reg_cos);

        // phase >= PI/2
        uint32x4_t ge1_2pi0 = vcgeq_s32(vphase0, v1_2pi);
        uint32x4_t ge1_2pi1 = vcgeq_s32(vphase1, v1_2pi);
        // phase < 3PI/2
        uint32x4_t lt3_2pi0 = vcltq_s32(vphase0, v3_2pi);
        uint32x4_t lt3_2pi1 = vcltq_s32(vphase1, v3_2pi);
        // phase >= PI/2 && phase < 3PI/2
        uint32x4_t sign0 = vandq_u32(ge1_2pi0, lt3_2pi0);
        uint32x4_t sign1 = vandq_u32(ge1_2pi1, lt3_2pi1);

        // take sign low i16 and pack
        uint16x8_t sign = vcombine_u16(vmovn_u32(sign0), vmovn_u32(sign1));

        uint16x8_t sign_sin_mask = veorq_u16(vsign_sin, sign);
        uint16x8_t sign_cos_mask = veorq_u16(vsign_cos, sign);

        int16x8x2_t res;
        res.val[0] = vbslq_s16(sign_sin_mask, vnegq_s16(reg_sin), reg_sin);
        res.val[1] = vbslq_s16(sign_cos_mask, vnegq_s16(reg_cos), reg_cos);
        vst2q_s16(outdata, res);

        outdata += 16;
        phase += (delta_phase << 3);
        phase &= (WVLT_SINCOS_I16_TWO_PI - 1);
        i -= 8;
    }

#undef WVLT_SINCOS

    while(i > 0)
    {
        const float ph = WVLT_SINCOS_I16_PHSCALE * phase;
        float ssin, scos;

        sincosf(ph, &ssin, &scos);
        *outdata++ = ssin * WVLT_SINCOS_I16_SCALE * sign_sin;
        *outdata++ = scos * WVLT_SINCOS_I16_SCALE * sign_cos;

        phase += delta_phase;
        phase &= (WVLT_SINCOS_I16_TWO_PI - 1);
        --i;
    }

    *start_phase = phase;
}

#undef TEMPLATE_FUNC_NAME
