#define CHIRP_PHASE_CONDITION \
if (++step_idx > (uint32_t)chirp_steps_cnt) { dphase = reset_dphase; phase = 0; step_idx = 0; } \
else { phase += dphase; dphase += chirp_step; }

#undef CHIRP_DEBUG_PRINT

static
void TEMPLATE_FUNC_NAME(int32_t *__restrict start_phase, int32_t *__restrict start_delta_phase,
                        int32_t *__restrict delta_phase,
                        int32_t chirp_samples_count,
                        int16_t gain,
                        bool inv_sin,
                        bool inv_cos,
                        int16_t *__restrict outdata,
                        unsigned total_samples_count)
{
    unsigned i = total_samples_count;

    int32_t chirp_steps_cnt;
    if(chirp_samples_count > 0) {
        chirp_steps_cnt = chirp_samples_count - 1;
    } else {
        chirp_steps_cnt = chirp_samples_count + 1;
    }

    int32_t delta_phase0 = delta_phase[0];
    int32_t delta_phase1 = delta_phase[1];

    const int16_t sign_sin = inv_sin ? -1 : 1;
    const int16_t sign_cos = inv_cos ? -1 : 1;

#ifdef CHIRP_DEBUG_PRINT
    printf("ORIGINAL F0:%d F1:%d PHASE:%d F:%d\n", delta_phase0, delta_phase1, *start_phase, *start_delta_phase);
#endif

    if((int64_t)delta_phase1 * (int64_t)delta_phase0 > 0) {
        return;
    }
    if(delta_phase0 > delta_phase1) {
        return;
    }
    if(delta_phase0 < -delta_phase1) {
        delta_phase0 = -delta_phase1;
    } else if(delta_phase0 > -delta_phase1) {
        delta_phase1 = -delta_phase0;
    }

    const int64_t pdiff = (int64_t)delta_phase1 - (int64_t)delta_phase0;
    const int64_t rem = pdiff % abs(chirp_steps_cnt);
    delta_phase0 += (rem >> 1);
    delta_phase1 -= rem - (rem >> 1);
    const int32_t chirp_step = (pdiff - rem) / chirp_steps_cnt;

    int32_t dphase = *start_delta_phase;
    if(dphase > delta_phase1) {
        dphase = delta_phase1;
    } else if(dphase < delta_phase0) {
        dphase = delta_phase0;
    }

    uint32_t step_idx = 0;
    if(chirp_step > 0) {
        step_idx = (uint32_t)(dphase - delta_phase0) / chirp_step;
    } else if(chirp_step < 0) {
        step_idx = (uint32_t)(delta_phase1 - dphase) / (-chirp_step);
    }

    const int32_t reset_dphase = (chirp_step >= 0) ? delta_phase0 : delta_phase1;
    chirp_steps_cnt = abs(chirp_steps_cnt);

    int32_t phase = *start_phase;
    if (step_idx == 0) {
        phase = 0;
    }

#ifdef CHIRP_DEBUG_PRINT
    printf("ADJUSTED F0:%d F1:%d\n", delta_phase0, delta_phase1);
    printf("STEPS CNT:%d\n", chirp_steps_cnt);
    printf("CHIRP STEP:%d\n", chirp_step);
    printf("STEP IDX:%u\n", step_idx);

    printf(">>START: PHASE:%d F:%d\n", phase, dphase);
#endif

#ifdef USE_SSSE3_CHIRP
    #include "wvlt_sincos_i16_interleaved_chirp_ssse3.inc"
#else
    #include "wvlt_sincos_i16_interleaved_chirp_generic.inc"
#endif

    *start_phase = phase;
    *start_delta_phase = dphase;

#ifdef CHIRP_DEBUG_PRINT
    printf(">>END: PHASE:%d F:%d\n", phase, dphase);
#endif
}

#undef TEMPLATE_FUNC_NAME
