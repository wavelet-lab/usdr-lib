    while(i > 0)
    {
        phase += dphase;
        const float ph = WVLT_SINCOS_I32_PHSCALE * phase;
        float ssin, scos;

        sincosf(ph, &ssin, &scos);
        *outdata++ = ssin * gain * sign_sin;
        *outdata++ = scos * gain * sign_cos;

        dphase += chirp_step;
        if(dphase > delta_phase1)
        {
            dphase = delta_phase0;
        }
        else if(dphase < delta_phase0)
        {
            dphase = delta_phase1;
        }

        --i;
    }
