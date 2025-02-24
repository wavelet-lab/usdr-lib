#define CONV_I16_F32(ptr_in) vmulq_n_f32(vcvtq_f32_s32(vmovl_s16(vld1_s16(ptr_in))), CONV_SCALE)

#define CONV_F32_I16(f0, f1, ptr_out) \
{ \
    int32x4_t n0 = vcvtq_s32_f32(vmulq_n_f32((f0), scale)); \
    int32x4_t n1 = vcvtq_s32_f32(vmulq_n_f32((f1), scale)); \
    vst1q_s16((ptr_out), vcombine_s16(vqmovn_s32(n0), vqmovn_s32(n1))); \
}
