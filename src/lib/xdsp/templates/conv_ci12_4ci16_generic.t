static
void TEMPLATE_FUNC_NAME(const void *__restrict indata_p,
                        unsigned indatabsz,
                        void *__restrict outdata_0_p,
                        void *__restrict outdata_1_p,
                        void *__restrict outdata_2_p,
                        void *__restrict outdata_3_p,
                        unsigned outdatabsz)
{
    unsigned i = indatabsz;
    /* 12 bits -> 16 bits  =>  3 -> 4   */
    if ((outdatabsz * 3 / 4) < i)
        i = (outdatabsz * 3 / 4);

    const uint8_t* indata = (const uint8_t*)indata_p;
    int16_t* outdata_0 = (int16_t*)outdata_0_p;
    int16_t* outdata_1 = (int16_t*)outdata_1_p;
    int16_t* outdata_2 = (int16_t*)outdata_2_p;
    int16_t* outdata_3 = (int16_t*)outdata_3_p;

    for (; i >= 12; i -= 12) {
        /* read 12 bytes -> 4ci16 */

        uint64_t v0 = *(const uint64_t *)(indata + 0);
        uint64_t v1 = *(const uint64_t *)(indata + 6);
        indata += 12;

        *(outdata_0++) = (int16_t)((v0 <<  4)         );
        *(outdata_0++) = (int16_t)((v0 >>  8) & 0xfff0);
        *(outdata_1++) = (int16_t)((v0 >> 20) & 0xfff0);
        *(outdata_1++) = (int16_t)((v0 >> 32) & 0xfff0);
        *(outdata_2++) = (int16_t)((v1 <<  4)         );
        *(outdata_2++) = (int16_t)((v1 >>  8) & 0xfff0);
        *(outdata_3++) = (int16_t)((v1 >> 20) & 0xfff0);
        *(outdata_3++) = (int16_t)((v1 >> 32) & 0xfff0);
    }

    // tail ignored
}

#undef TEMPLATE_FUNC_NAME
