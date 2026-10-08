    while(i > 0) {
        const float ph = WVLT_SINCOS_I32_PHSCALE * phase;
        float ssin, scos;

        sincosf(ph, &ssin, &scos);
        *outdata++ = ssin * gain * sign_sin;
        *outdata++ = scos * gain * sign_cos;

#ifdef CHIRP_DEBUG_PRINT
        if(total_samples_count - i <= chirp_samples_count) {
            printf("---- GENERIC ---- step_idx:%u PHASE:%d F:%d\n", step_idx, phase, dphase);
        }
#endif

        CHIRP_PHASE_CONDITION;
        --i;
    }
