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
