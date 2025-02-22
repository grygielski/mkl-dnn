/*******************************************************************************
 * Copyright 2019 Intel Corporation
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 ******************************************************************************/

#ifndef JIT_AVX512_CORE_KERNEL_GEMV_S8U8S32_KERN_HPP
#define JIT_AVX512_CORE_KERNEL_GEMV_S8U8S32_KERN_HPP

#include <cstdint>

#include "../gemm_info.hpp"
#include "common_u8.hpp"
#include "jit_generator.hpp"

namespace dnnl {
namespace impl {
namespace cpu {

class jit_avx512_core_gemv_s8u8s32_kern : jit_generator {

    DECLARE_CPU_JIT_AUX_FUNCTIONS(jit_avx512_core_gemv_s8u8s32_kern);

    // Assumes unroll_{m,n} are a power of 2.
    static constexpr unsigned int unroll_m_ = 4; // Unrolling is 2^unroll_m_.
    const int mask_um_ = 0xFFFFFFF0;
    static constexpr unsigned int unroll_n_ = 6; // Unrolling is 2^unroll_n_.
    const int mask_un_ = 0xFFFFFFC0;
    const int size_vec_reg_ = 64; // bytes

    void aligned_label(Xbyak::Label &label, int alignment = 16) {
        align(alignment);
        L(label);
    }

    void vnni(Xbyak::Zmm, Xbyak::Zmm, Xbyak::Zmm, Xbyak::Zmm, Xbyak::Zmm, bool,
            int);
    void n_loop_body(int, int, int, int, Xbyak::Reg64, Xbyak::Reg64,
            Xbyak::Reg64, Xbyak::Zmm, Xbyak::Zmm, bool, int, int,
            Xbyak::Opmask);
    void shuffle_and_add(
            Xbyak::Zmm, Xbyak::Zmm, Xbyak::Zmm, Xbyak::Zmm, Xbyak::Zmm);
    void update_c(int, Xbyak::Reg64, int, int, Xbyak::Xmm, int, Xbyak::Opmask);

public:
    jit_avx512_core_gemv_s8u8s32_kern() : jit_generator(nullptr, 16384) {};

    // m, n, alpha, a, lda, x, beta, y
    typedef void (*gemv_s8u8s32_kernel_t)(const dim_t, const dim_t, const float,
            const int8_t *, const dim_t, const uint8_t *, const float,
            int32_t *);
    typedef void (*gemv_u8s8s32_kernel_t)(const dim_t, const dim_t, const float,
            const uint8_t *, const dim_t, const int8_t *, const float,
            int32_t *);

    template <typename T>
    T generate(int use_vnni);
};

} // namespace cpu
} // namespace impl
} // namespace dnnl

#endif // JIT_AVX512_CORE_KERNEL_GEMV_S8U8S32_KERN_HPP
