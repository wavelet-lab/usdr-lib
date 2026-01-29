static
void TEMPLATE_FUNC_NAME(const void *__restrict indata,
                        unsigned indatabsz,
                        void *__restrict outdata_0_p,
                        void *__restrict outdata_1_p,
                        void *__restrict outdata_2_p,
                        unsigned outdatabsz)
{
    unsigned i = indatabsz;
    if ((outdatabsz) < i)
        i = (outdatabsz);

    int16_t* outdata_0 = (int16_t*)outdata_0_p;
    int16_t* outdata_1 = (int16_t*)outdata_1_p;
    int16_t* outdata_2 = (int16_t*)outdata_2_p;

    const uint32_t *ld = (const uint32_t *)indata;
    #include "conv_ci16_3ci16_generic.inc"
}

#undef TEMPLATE_FUNC_NAME
