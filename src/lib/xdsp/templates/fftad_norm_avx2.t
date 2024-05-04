static
void TEMPLATE_FUNC_NAME(fft_acc_t* __restrict p, unsigned fftsz, float scale, float corr, double* __restrict outa)
{
    // TODO optimize this for higher FPS rates
    for (unsigned i = 0; i < fftsz; i += 8) {
        float apwr[8];
        int aidx[8];
        const int idx[] = { 0, 1, 4, 5, 2, 3, 6, 7 };

        for (unsigned k = 0; k < 8; k++) {
            apwr[k] = log2f(p->f_mant[i + idx[k]]);
            aidx[k] = p->f_pwr[i + idx[k]];

            float f = scale * (aidx[k] + apwr[k]) + corr;
            outa[(i + k) ^ (fftsz / 2)] = f;
        }
    }
}

#undef TEMPLATE_FUNC_NAME
