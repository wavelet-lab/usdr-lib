static
void TEMPLATE_FUNC_NAME(const void *__restrict indata_p,
                        unsigned indatabsz,
                        void *__restrict outdata_p,
                        unsigned outdatabsz)
{
    unsigned i = indatabsz;
    if ((outdatabsz * 4 / 3) < i)
        i = (outdatabsz * 4 / 3);

    const int16_t* indata = (const int16_t*)indata_p;
    uint8_t* outdata = (uint8_t*)outdata_p;

    #include "conv_i16_i12_generic.inc"
}

#undef TEMPLATE_FUNC_NAME
