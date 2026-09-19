static
void TEMPLATE_FUNC_NAME(int32_t *__restrict start_phase, int32_t *__restrict start_delta_phase,
                        int32_t *__restrict delta_phase,
                        int32_t chirp_steps_cnt,
                        int16_t gain,
                        bool inv_sin,
                        bool inv_cos,
                        int16_t *__restrict outdata,
                        unsigned iters)
{
    unsigned i = iters;

    int32_t phase = *start_phase;
    int32_t delta_phase0 = delta_phase[0];
    int32_t delta_phase1 = delta_phase[1];

    const int16_t sign_sin = inv_sin ? -1 : 1;
    const int16_t sign_cos = inv_cos ? -1 : 1;

    const int32_t chirp_step = ((int64_t)delta_phase1 - (int64_t)delta_phase0) / chirp_steps_cnt;
    int32_t dphase = *start_delta_phase;

    #include "wvlt_sincos_i16_interleaved_chirp_generic.inc"

    *start_phase = phase;
    *start_delta_phase = dphase;
}

#undef TEMPLATE_FUNC_NAME
