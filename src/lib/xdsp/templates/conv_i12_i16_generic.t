static
void TEMPLATE_FUNC_NAME(const void *__restrict indata_p,
                        unsigned indatabsz,
                        void *__restrict outdata_p,
                        unsigned outdatabsz)
{
    unsigned i = indatabsz;
    /* 12 bits -> 16 bits  =>  3 -> 4   */
    if ((outdatabsz * 3 / 4) < i)
        i = (outdatabsz * 3 / 4);

    const uint8_t* indata = (const uint8_t*)indata_p;
    int16_t* outdata = (int16_t*)outdata_p;

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
        const uint8_t v0 = *(indata++);
        const uint8_t v1 = *(indata++);
        const uint8_t v2 = *(indata++);
        i -= 3;

        const int16_t a = (int16_t) (((uint16_t)v0 << 4) | ((uint16_t)v1 << 12));
        const int16_t b = (int16_t) (((uint16_t)v2 << 8) | (v1 & 0xf0));

        *(outdata++) = a;
        *(outdata++) = b;
    }

    if(i >= 2)
    {
        const uint16_t v = *(const uint16_t*)indata;
        const int16_t a = (int16_t)(v << 4);
        *(outdata++) = a;
        i -= 2;
    }
}

#undef TEMPLATE_FUNC_NAME
