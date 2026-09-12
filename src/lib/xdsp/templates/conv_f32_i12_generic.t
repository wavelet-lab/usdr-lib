static
void TEMPLATE_FUNC_NAME(const void *__restrict indata_p,
                        unsigned indatabsz,
                        void *__restrict outdata_p,
                        unsigned outdatabsz)
{
    unsigned i = indatabsz;
    if ((outdatabsz * 8 / 3) < i)
        i = (outdatabsz * 8 / 3);

    const float* indata = (const float*)indata_p;
    uint8_t* outdata = (uint8_t*)outdata_p;

    #include "conv_f32_i12_generic.inc"
}

#undef TEMPLATE_FUNC_NAME
