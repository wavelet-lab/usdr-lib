static
void TEMPLATE_FUNC_NAME(const void *__restrict indata_p,
                        unsigned indatabsz,
                        void *__restrict outdata_p,
                        unsigned outdatabsz)
{
    unsigned i = indatabsz;
    /* 12 bits -> 32 bits  =>  3 -> 8   */
    if ((outdatabsz * 3 / 8) < i)
        i = (outdatabsz * 3 / 8);

    const uint64_t* indata = (const uint64_t*)indata_p;
    float* outdata = (float*)outdata_p;

    for (; i >= 6; i -= 6) {
    	/* read 48 bits -> 4 floats */
    	
        uint64_t v = *(const uint64_t *)indata;
        indata += 6;
        
        float a = (int16_t)(v << 4);
        float b = (int16_t)((v >> 8) & 0xfff0);
        float c = (int16_t)((v >> 20) & 0xfff0);
        float d = (int16_t)((v >> 32)  & 0xfff0);

        *(outdata++) = a * CONV_SCALE;
        *(outdata++) = b * CONV_SCALE;
        *(outdata++) = c * CONV_SCALE;
        *(outdata++) = d * CONV_SCALE;
    }
    
    /* TODO: remaining */
}

#undef TEMPLATE_FUNC_NAME
