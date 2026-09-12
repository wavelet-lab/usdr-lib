static
void TEMPLATE_FUNC_NAME(const void *__restrict indata_p,
                        unsigned indatabsz,
                        void *__restrict outdata_p,
                        unsigned outdatabsz)
{
    unsigned i = indatabsz;
    if ((outdatabsz * 4 / 3) < i)
        i = (outdatabsz * 4 / 3);

    const void* raw_ptr = indata_p;
    uint64_t *out64 = (uint64_t*)outdata_p;

    //AVX512 block
    {
        #include "conv_i16_i12_avx512bw.inc"

        const __m512i *in512 = (__m512i*)raw_ptr;
        __m512i  v0, v1;

        for (; i >= 64*2; i -= 64*2)
        {
            v0 = _mm512_loadu_si512(in512 + 0);
            v1 = _mm512_loadu_si512(in512 + 1);
            in512 += 2;

            CONVERT_I16_I12_BLOCK(v0, out64);
            CONVERT_I16_I12_BLOCK(v1, out64);
        }

        for (; i >= 64; i -= 64)
        {
            v0 = _mm512_loadu_si512(in512++);
            CONVERT_I16_I12_BLOCK(v0, out64);
        }

        raw_ptr = (void*)in512;
        #undef CONVERT_I16_I12_BLOCK
    }

    //AVX2 block
    {
        #include "conv_i16_i12_avx2.inc"

        const __m256i *in256 = (__m256i*)raw_ptr;
        __m256i v0;

        if(i >= 32)
        {
            v0 = _mm256_loadu_si256(in256++);
            CONVERT_I16_I12_BLOCK(v0, out64);
            i -= 32;
        }

        raw_ptr = (void*)in256;
        #undef CONVERT_I16_I12_BLOCK
    }

    //Generic block
    {
        const int16_t* indata = (const int16_t*)raw_ptr;
        uint8_t* outdata = (uint8_t*)out64;

        #include "conv_i16_i12_generic.inc"
    }
}

#undef TEMPLATE_FUNC_NAME
