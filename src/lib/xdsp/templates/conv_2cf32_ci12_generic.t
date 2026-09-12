static
void TEMPLATE_FUNC_NAME(const void *__restrict indata_0_p,
                        const void *__restrict indata_1_p,
                        unsigned indatabsz,
                        void *__restrict outdata_p,
                        unsigned outdatabsz)
{
    unsigned i = indatabsz;
    if ((outdatabsz * 8 / 3) < i)
        i = (outdatabsz * 8 / 3);

    const float* indata_0 = (const float*)indata_0_p;
    const float* indata_1 = (const float*)indata_1_p;
    uint8_t* outdata = (uint8_t*)outdata_p;

    #include "conv_2cf32_ci12_generic.inc"
}
#undef TEMPLATE_FUNC_NAME
