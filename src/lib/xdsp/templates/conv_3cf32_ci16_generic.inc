    for (; i >= 24; i -= 24, indata_0 += 2, indata_1 += 2, indata_2 += 2, outdata += 6) {
        int16_t a = indata_0[0] / CONV_SCALE;
        int16_t b = indata_0[1] / CONV_SCALE;
        int16_t c = indata_1[0] / CONV_SCALE;
        int16_t d = indata_1[1] / CONV_SCALE;
        int16_t e = indata_2[0] / CONV_SCALE;
        int16_t f = indata_2[1] / CONV_SCALE;

        uint64_t v = (uint64_t)(uint16_t)a | ((uint64_t)(uint16_t)b << 16) | ((uint64_t)(uint16_t)c << 32) | ((uint64_t)(uint16_t)d << 48);
        uint32_t w = (uint32_t)(uint16_t)e | ((uint32_t)(uint16_t)f << 16);
        *(uint64_t*)outdata = v;
        *(uint32_t*)(outdata + 4) = w;
    }
    // do nothing with leftover
