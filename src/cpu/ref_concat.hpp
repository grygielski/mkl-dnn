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

#ifndef REF_CONCAT_HPP
#define REF_CONCAT_HPP

#include "engine.hpp"
#include "reorder_pd.hpp"

#include "cpu_concat_pd.hpp"

namespace dnnl {
namespace impl {
namespace cpu {

struct ref_concat_t : public primitive_impl_t {
    struct pd_t : public cpu_concat_pd_t {
        using cpu_concat_pd_t::cpu_concat_pd_t;

        pd_t(const pd_t &rhs) : cpu_concat_pd_t(rhs) { clone_reorder_pds(rhs); }

        ~pd_t() { clear(); }

        pd_t &operator=(const pd_t &rhs) {
            DNNL_SHORT_CIRCUIT_SELF_ASSIGN(rhs);
            cpu_concat_pd_t::operator=(rhs);
            clear();
            clone_reorder_pds(rhs);
            return *this;
        }

        DECLARE_CONCAT_PD_T("ref:any", ref_concat_t);

        status_t init() {
            bool ok = cpu_concat_pd_t::init() == status::success;
            if (!ok) return status::unimplemented;

            for (int i = 0; i < n_; ++i) {
                auto r_impls = engine_->get_reorder_implementation_list();
                for (auto r = r_impls; *r; ++r) {
                    const primitive_attr_t attr; /* alpha == 1. */
                    reorder_pd_t *r_pd = nullptr;
                    if ((*r)(&r_pd, engine_, &attr, engine_, src_md(i), engine_,
                                src_image_md(i))
                            == status::success) {
                        reorder_pds_.push_back(r_pd);
                        break;
                    }
                }
            }

            ok = reorder_pds_.size() == (size_t)n_;
            return ok ? status::success : status::unimplemented;
        }

        void clone_reorder_pds(const pd_t &rhs) {
            for (size_t i = 0; i < rhs.reorder_pds_.size(); ++i)
                reorder_pds_.push_back(
                        (const reorder_pd_t *)rhs.reorder_pds_[i]->clone());
        }

        void clear() {
            for (auto &rpd : reorder_pds_)
                delete rpd;
        }

        std::vector<const reorder_pd_t *> reorder_pds_;
    };

    ref_concat_t(const pd_t *apd) : primitive_impl_t(apd) {
        const int n = pd()->n_inputs();
        reorders_.resize(n);
        for (int i = 0; i < n; ++i)
            pd()->reorder_pds_[i]->create_primitive(&reorders_[i]);
    }

    ~ref_concat_t() {
        for (auto &r : reorders_)
            delete r;
    }

    virtual status_t execute(const exec_ctx_t &ctx) const override {
        const auto n = pd()->n_inputs();
        for (int i = 0; i < n; ++i) {
            exec_args_t r_args;
            r_args[DNNL_ARG_SRC] = ctx.args().at(DNNL_ARG_MULTIPLE_SRC + i);
            r_args[DNNL_ARG_DST] = ctx.args().at(DNNL_ARG_DST);
            exec_ctx_t r_ctx(ctx.stream(), std::move(r_args));
            reorders_[i]->execute(r_ctx);
        }
        return status::success;
    }

private:
    const pd_t *pd() const { return (const pd_t *)primitive_impl_t::pd(); }
    std::vector<primitive_t *> reorders_;
};

} // namespace cpu
} // namespace impl
} // namespace dnnl

#endif
