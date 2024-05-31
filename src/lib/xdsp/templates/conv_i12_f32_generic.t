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

    const uint8_t* indata = (const uint8_t*)indata_p;
    float* outdata = (float*)outdata_p;

#if 0   //this algorithm is slow
    for (; i >= 3 * sizeof(uint64_t); i -= 3 * sizeof(uint64_t)) {
        /* read 64*3 = 192 bits -> 16 i32 & floats */
    	
        uint64_t v2 = *(const uint64_t *)(indata +  0);
        uint64_t v1 = *(const uint64_t *)(indata +  8);
        uint64_t v0 = *(const uint64_t *)(indata + 16);

        indata += 3 * sizeof(uint64_t);
        
        float f0  = (int16_t)(v2 << 4);
        float f1  = (int16_t)((v2 >>  8) & 0xfff0);
        float f2  = (int16_t)((v2 >> 20) & 0xfff0);
        float f3  = (int16_t)((v2 >> 32) & 0xfff0);
        float f4  = (int16_t)((v2 >> 44) & 0xfff0);

        float f5  = ((int16_t)(v2 >> 56) & 0x00f0) | (int16_t)(v1 << 8);

        float f6  = (int16_t)((v1 >>  4) & 0xfff0);
        float f7  = (int16_t)((v1 >> 16) & 0xfff0);
        float f8  = (int16_t)((v1 >> 28) & 0xfff0);
        float f9  = (int16_t)((v1 >> 40) & 0xfff0);

        float f10 = (int16_t)((v1 >> 52) & 0x0ff0) | (int16_t)(v0 << 12);

        float f11 = (int16_t)(v0 & 0xfff0);
        float f12 = (int16_t)((v0 >> 12) & 0xfff0);
        float f13 = (int16_t)((v0 >> 24) & 0xfff0);
        float f14 = (int16_t)((v0 >> 36) & 0xfff0);
        float f15 = (int16_t)((v0 >> 48) & 0xfff0);

        *(outdata++) = f0  * CONV_SCALE;
        *(outdata++) = f1  * CONV_SCALE;
        *(outdata++) = f2  * CONV_SCALE;
        *(outdata++) = f3  * CONV_SCALE;
        *(outdata++) = f4  * CONV_SCALE;
        *(outdata++) = f5  * CONV_SCALE;
        *(outdata++) = f6  * CONV_SCALE;
        *(outdata++) = f7  * CONV_SCALE;
        *(outdata++) = f8  * CONV_SCALE;
        *(outdata++) = f9  * CONV_SCALE;
        *(outdata++) = f10 * CONV_SCALE;
        *(outdata++) = f11 * CONV_SCALE;
        *(outdata++) = f12 * CONV_SCALE;
        *(outdata++) = f13 * CONV_SCALE;
        *(outdata++) = f14 * CONV_SCALE;
        *(outdata++) = f15 * CONV_SCALE;
    }
#endif

    /*  memory stream:  MSB...LSB
    * 0x00     f0[7:0]
    * 0x01     {f1[3:0],f0[11:8]}
    * 0x02     f1[11:4]
    * .....
    */

    /*     v2    v1    v0
    *  +-----+-----+-----+
    *  |  8  |  8  |  8  |
    *  +-----+-----+-----+
    *  |  12    |   12   |
    *  +-----+-----+-----+
    *
    *        +-----+-----+
    *  as =  |v1|  v0 |00|
    *        +-----+-----+
    *  bs =  | v2  |v1|00|
    *        +-----+-----+
    */

    while(i >= 3)
    {
        uint8_t v0 = *(indata++);
        uint8_t v1 = *(indata++);
        uint8_t v2 = *(indata++);
        i -= 3;

        float a = (int16_t) (((uint16_t)v0 << 4) | ((uint16_t)v1 << 12));
        float b = (int16_t) (((uint16_t)v2 << 8) | (v1 & 0xf0));

        *(outdata++) = a * CONV_SCALE;
        *(outdata++) = b * CONV_SCALE;
    }

    if(i >= 2)
    {
        uint16_t v = *(const uint16_t*)indata;
        float a = (int16_t)(v << 4);
        *(outdata++) = a * CONV_SCALE;
        i -= 2;
    }

    if(i)
    {
        *outdata = 0;
    }
}

#undef TEMPLATE_FUNC_NAME
