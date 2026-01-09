
static
void TEMPLATE_FUNC_NAME(const void *__restrict indata_0_p,
                        const void *__restrict indata_1_p,
                        const void *__restrict indata_2_p,
                        unsigned indatabsz,
                        void *__restrict outdata_p,
                        unsigned outdatabsz)
{
    unsigned i = indatabsz;
    if ((outdatabsz) < i)
        i = (outdatabsz);

    const int16_t* indata_0 = (int16_t*)indata_0_p;
    const int16_t* indata_1 = (int16_t*)indata_1_p;
    const int16_t* indata_2 = (int16_t*)indata_2_p;

    int16_t* outdata = (int16_t*)outdata_p;
    #include "conv_3ci16_ci16_generic.inc"
}

#undef TEMPLATE_FUNC_NAME
