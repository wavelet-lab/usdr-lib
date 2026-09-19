for (; i >= 24; i -= 24)
{
    *outdata_0++ = *indata++;
    *outdata_1++ = *indata++;
    *outdata_2++ = *indata++;
    *outdata_3++ = *indata++;
    *outdata_4++ = *indata++;
    *outdata_5++ = *indata++;
}
// do nothing with leftover
