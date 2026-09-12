static
void TEMPLATE_FUNC_NAME(const void *__restrict indata,
                        unsigned indatabsz,
                        void *__restrict outdata_0_p,
                        void *__restrict outdata_1_p,
                        void *__restrict outdata_2_p,
                        void *__restrict outdata_3_p,
                        void *__restrict outdata_4_p,
                        void *__restrict outdata_5_p,
                        void *__restrict outdata_6_p,
                        void *__restrict outdata_7_p,
                        unsigned outdatabsz)
{
    unsigned i = indatabsz;
    if ((outdatabsz / 2) < i)
        i = (outdatabsz / 2);

    const uint64_t *ld = (const uint64_t *)indata;

    float* outdata_0 = (float*)outdata_0_p;
    float* outdata_1 = (float*)outdata_1_p;
    float* outdata_2 = (float*)outdata_2_p;
    float* outdata_3 = (float*)outdata_3_p;
    float* outdata_4 = (float*)outdata_4_p;
    float* outdata_5 = (float*)outdata_5_p;
    float* outdata_6 = (float*)outdata_6_p;
    float* outdata_7 = (float*)outdata_7_p;

    #include "conv_ci16_8cf32_generic.inc"
}

#undef TEMPLATE_FUNC_NAME
