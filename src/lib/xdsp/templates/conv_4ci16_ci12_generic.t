static
void TEMPLATE_FUNC_NAME(const void *__restrict indata_0_p,
                        const void *__restrict indata_1_p,
                        const void *__restrict indata_2_p,
                        const void *__restrict indata_3_p,
                        unsigned indatabsz,
                        void *__restrict outdata_p,
                        unsigned outdatabsz)
{
    unsigned i = indatabsz;
    if ((outdatabsz * 4 / 3) < i)
        i = (outdatabsz * 4 / 3);

    const int16_t* indata_0 = (const int16_t*)indata_0_p;
    const int16_t* indata_1 = (const int16_t*)indata_1_p;
    const int16_t* indata_2 = (const int16_t*)indata_2_p;
    const int16_t* indata_3 = (const int16_t*)indata_3_p;

    uint8_t* outdata = (uint8_t*)outdata_p;

    #include "conv_4ci16_ci12_generic.inc"
}
#undef TEMPLATE_FUNC_NAME
