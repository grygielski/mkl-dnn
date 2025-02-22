/*******************************************************************************
* Copyright 2017-2018 Intel Corporation
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
*******************************************************************************/

#ifndef CPU_JIT_UNI_POOLING_HPP
#define CPU_JIT_UNI_POOLING_HPP

#include <assert.h>

#include "c_types_map.hpp"
#include "type_helpers.hpp"
#include "utils.hpp"

#include "cpu_pooling_pd.hpp"
#include "jit_uni_pool_kernel.hpp"

namespace dnnl {
namespace impl {
namespace cpu {

template <cpu_isa_t isa, impl::data_type_t d_type>
struct jit_uni_pooling_fwd_t : public primitive_impl_t {
    struct pd_t : public cpu_pooling_fwd_pd_t {
        using cpu_pooling_fwd_pd_t::cpu_pooling_fwd_pd_t;

        DECLARE_COMMON_PD_T(
                JIT_IMPL_NAME_HELPER("jit:", isa, ""), jit_uni_pooling_fwd_t);

        status_t init() {
            using namespace utils;

            bool ok = true && set_default_params() == status::success
                    && is_fwd() && !has_zero_dim_memory()
                    && everyone_is(
                            d_type, src_md()->data_type, dst_md()->data_type)
                    && attr()->has_default_values()
                    && memory_desc_matches_tag(*src_md(), desired_fmt_tag())
                    && memory_desc_matches_tag(*dst_md(), desired_fmt_tag());
            if (!ok) return status::unimplemented;

            bool is_training = desc_.prop_kind == prop_kind::forward_training;
            if (desc()->alg_kind == alg_kind::pooling_max && is_training)
                init_default_ws();

            return jit_uni_pool_kernel<isa>::init_conf(jpp_, this);
        }

        format_tag_t desired_fmt_tag() {
            using namespace format_tag;
            return utils::one_of(isa, avx512_common, avx512_core)
                    ? (ndims() == 4 ? nChw16c : nCdhw16c)
                    : (ndims() == 4 ? nChw8c : nCdhw8c);
        }

        jit_pool_conf_t jpp_;
    };

    jit_uni_pooling_fwd_t(const pd_t *apd) : primitive_impl_t(apd) {
        kernel_ = new jit_uni_pool_kernel<isa>(pd()->jpp_);
    }

    ~jit_uni_pooling_fwd_t() { delete kernel_; }

    typedef typename prec_traits<d_type>::type data_t;

    virtual status_t execute(const exec_ctx_t &ctx) const override {
        auto src = CTX_IN_MEM(const data_t *, DNNL_ARG_SRC);
        auto dst = CTX_OUT_MEM(data_t *, DNNL_ARG_DST);
        auto ws = CTX_OUT_MEM(char *, DNNL_ARG_WORKSPACE);

        if (pd()->ndims() == 5)
            execute_forward_3d(src, dst, ws);
        else
            execute_forward(src, dst, ws);

        return status::success;
    }

private:
    void execute_forward(const data_t *src, data_t *dst, char *indices) const;
    void execute_forward_3d(
            const data_t *src, data_t *dst, char *indices) const;
    const pd_t *pd() const { return (const pd_t *)primitive_impl_t::pd(); }
    jit_uni_pool_kernel<isa> *kernel_;
};

template <cpu_isa_t isa, impl::data_type_t d_type>
struct jit_uni_pooling_bwd_t : public primitive_impl_t {
    struct pd_t : public cpu_pooling_bwd_pd_t {
        using cpu_pooling_bwd_pd_t::cpu_pooling_bwd_pd_t;

        DECLARE_COMMON_PD_T(
                JIT_IMPL_NAME_HELPER("jit:", isa, ""), jit_uni_pooling_bwd_t);

        status_t init() {
            using namespace utils;

            bool ok = true && set_default_params() == status::success
                    && !is_fwd() && !has_zero_dim_memory()
                    && everyone_is(d_type, diff_src_md()->data_type,
                            diff_dst_md()->data_type)
                    && attr()->has_default_values()
                    && memory_desc_matches_tag(
                            *diff_dst_md(), desired_fmt_tag())
                    && memory_desc_matches_tag(
                            *diff_src_md(), desired_fmt_tag());
            if (!ok) return status::unimplemented;

            if (desc()->alg_kind == alg_kind::pooling_max) {
                init_default_ws();
                if (!compare_ws(hint_fwd_pd_)) return status::unimplemented;
            }

            return jit_uni_pool_kernel<isa>::init_conf(jpp_, this);
        }

        format_tag_t desired_fmt_tag() {
            using namespace format_tag;
            return utils::one_of(isa, avx512_common, avx512_core)
                    ? (ndims() == 4 ? nChw16c : nCdhw16c)
                    : (ndims() == 4 ? nChw8c : nCdhw8c);
        }

        jit_pool_conf_t jpp_;
    };

    jit_uni_pooling_bwd_t(const pd_t *apd) : primitive_impl_t(apd) {
        kernel_ = new jit_uni_pool_kernel<isa>(pd()->jpp_);
    }

    ~jit_uni_pooling_bwd_t() { delete kernel_; }

    typedef typename prec_traits<d_type>::type data_t;

    virtual status_t execute(const exec_ctx_t &ctx) const override {
        auto diff_dst = CTX_IN_MEM(const data_t *, DNNL_ARG_DIFF_DST);
        auto ws = CTX_IN_MEM(const char *, DNNL_ARG_WORKSPACE);
        auto diff_src = CTX_OUT_MEM(data_t *, DNNL_ARG_DIFF_SRC);

        if (pd()->ndims() == 5)
            execute_backward_3d(diff_dst, ws, diff_src);
        else
            execute_backward(diff_dst, ws, diff_src);

        return status::success;
    }

private:
    void execute_backward(const data_t *diff_dst, const char *indices,
            data_t *diff_src) const;
    void execute_backward_3d(const data_t *diff_dst, const char *indices,
            data_t *diff_src) const;
    const pd_t *pd() const { return (const pd_t *)primitive_impl_t::pd(); }
    jit_uni_pool_kernel<isa> *kernel_;
};

} // namespace cpu
} // namespace impl
} // namespace dnnl

#endif

// vim: et ts=4 sw=4 cindent cino+=l0,\:4,N-s
