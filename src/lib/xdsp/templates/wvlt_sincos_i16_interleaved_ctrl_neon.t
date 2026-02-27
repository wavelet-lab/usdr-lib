static
void TEMPLATE_FUNC_NAME(int32_t *__restrict start_phase,
                        int32_t delta_phase,
                        int16_t gain,
                        bool inv_sin,
                        bool inv_cos,
                        int16_t *__restrict outdata,
                        unsigned iters)
{
    unsigned i = iters;
    int32_t phase = *start_phase;
    const int16_t sign_sin = inv_sin ? -1 : 1;
    const int16_t sign_cos = inv_cos ? -1 : 1;

    const uint16x8_t vsign_sin = vdupq_n_u16(inv_sin ? 0xffff : 0);
    const uint16x8_t vsign_cos = vdupq_n_u16(inv_cos ? 0xffff : 0);

    const int32x4_t vdeltap0 = {0 * delta_phase, 1 * delta_phase, 2 * delta_phase, 3 * delta_phase};
    const int32x4_t vdeltap1 = {4 * delta_phase, 5 * delta_phase, 6 * delta_phase, 7 * delta_phase};

    const int32x4_t mpi_2v = vdupq_n_s32(-32768);
    const int32x4_t  pi_2v = vdupq_n_s32( 32767);

#include "wvlt_sincos_i16_neon.inc"

    while(i >= 8)
    {
        // add delta
        int32x4_t vphase  = vdupq_n_s32(phase);
        int32x4_t vphase0 = vaddq_s32(vdeltap0, vphase);
        int32x4_t vphase1 = vaddq_s32(vdeltap1, vphase);

        int32x4_t ph0 = vshrq_n_s32(vphase0, 15);
        int32x4_t ph1 = vshrq_n_s32(vphase1, 15);

        uint32x4_t rflag0 = vorrq_u32(vcgtq_s32(ph0, pi_2v), vcltq_s32(ph0, mpi_2v));
        uint32x4_t rflag1 = vorrq_u32(vcgtq_s32(ph1, pi_2v), vcltq_s32(ph1, mpi_2v));
        uint16x8_t sign = vcombine_u16(vmovn_u32(rflag0), vmovn_u32(rflag1));

        // take phase low i16 and pack
        int16x8_t reg_phase = vcombine_s16(vmovn_s32(ph0), vmovn_s32(ph1));
        int16x8_t reg_sin, reg_cos;
        WVLT_SINCOS(reg_phase, reg_sin, reg_cos);

        // apply amplitude normalization
        reg_sin = vqrdmulhq_n_s16(reg_sin, gain);
        reg_cos = vqrdmulhq_n_s16(reg_cos, gain);

        uint16x8_t sign_sin_mask = veorq_u16(vsign_sin, sign);
        uint16x8_t sign_cos_mask = veorq_u16(vsign_cos, sign);

        int16x8x2_t res;
        res.val[0] = vbslq_s16(sign_sin_mask, vnegq_s16(reg_sin), reg_sin);
        res.val[1] = vbslq_s16(sign_cos_mask, vnegq_s16(reg_cos), reg_cos);
        vst2q_s16(outdata, res);

        outdata += 16;
        phase += (delta_phase << 3);
        i -= 8;
    }

#undef WVLT_SINCOS

    while(i > 0)
    {
        const float ph = WVLT_SINCOS_I32_PHSCALE * phase;
        float ssin, scos;

        sincosf(ph, &ssin, &scos);
        *outdata++ = ssin * gain * sign_sin;
        *outdata++ = scos * gain * sign_cos;

        phase += delta_phase;
        --i;
    }

    *start_phase = phase;
}

#undef TEMPLATE_FUNC_NAME
