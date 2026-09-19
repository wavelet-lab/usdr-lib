for (; i >= 32; i -= 32)
{
    *outdata_0++ = *in++;
    *outdata_1++ = *in++;
    *outdata_2++ = *in++;
    *outdata_3++ = *in++;
    *outdata_4++ = *in++;
    *outdata_5++ = *in++;
    *outdata_6++ = *in++;
    *outdata_7++ = *in++;
}

// do nothing with leftover
