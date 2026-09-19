float **dest = &outdata_0;

while(i >= 3)
{
    uint8_t v0 = *(indata++);
    uint8_t v1 = *(indata++);
    uint8_t v2 = *(indata++);
    i -= 3;

    float a = (int16_t) (((uint16_t)v0 << 4) | ((uint16_t)v1 << 12));
    float b = (int16_t) (((uint16_t)v2 << 8) | (v1 & 0xf0));

    *((*dest)++) = a * CONV_SCALE;
    *((*dest)++) = b * CONV_SCALE;

    dest = (*dest == outdata_0) ? &outdata_1 : &outdata_0;
}

if(i >= 2)
{
    uint16_t v = *(const uint16_t*)indata;
    float a = (int16_t)(v << 4);
    *((*dest)++) = a * CONV_SCALE;
    i -= 2;
}
