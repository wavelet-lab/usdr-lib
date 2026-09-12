    for (; i >= 12; i -= 12, ld += 3) {
        uint64_t v = *((uint64_t*)ld);
        uint32_t w = *(ld + 2);

        *(outdata_0++) = (int16_t)(v);
        *(outdata_0++) = (int16_t)(v>>16);
        *(outdata_1++) = (int16_t)(v>>32);
        *(outdata_1++) = (int16_t)(v>>48);
        *(outdata_2++) = (int16_t)(w);
        *(outdata_2++) = (int16_t)(w>>16);
    }
    // do nothing with leftover
