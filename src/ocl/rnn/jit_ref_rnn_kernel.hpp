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
*******************************************************************************/

#ifndef JIT_REF_RNN_KERNEL_HPP
#define JIT_REF_RNN_KERNEL_HPP

#include "common/c_types_map.hpp"
#include "compute/compute.hpp"

#include "ocl/jit_primitive_conf.hpp"

#include "ocl/rnn/ocl_rnn_pd.hpp"
#include "ocl/rnn/rnn_utils.hpp"

#define DEBUGPRINT 0
#if DEBUGPRINT
#define DPRINT(fmt, ...) \
    printf(fmt, __VA_ARGS__); \
    fflush(0)
#define WS_PRINT(s, w) ws_print(s, w)
#else
#define DPRINT(fmt, ...)
#define WS_PRINT(s, w)
#endif

namespace dnnl {
namespace impl {
namespace ocl {

struct jit_ref_rnn_kernel {

    jit_ref_rnn_kernel(jit_rnn_conf_t ajrnn) : jrnn(ajrnn) {};

    ~jit_ref_rnn_kernel() {};

    static status_t init_conf(jit_rnn_conf_t &jrnn,
            const rnn_utils::rnn_conf_t &rnn_conf, const rnn_pd_t *rnn_pd,
            const memory_desc_wrapper &src_layer_d,
            const memory_desc_wrapper &src_iter_d,
            const memory_desc_wrapper &src_iter_c_d,
            const memory_desc_wrapper &weights_layer_d,
            const memory_desc_wrapper &weights_iter_d,
            const memory_desc_wrapper &bias_d,
            const memory_desc_wrapper &dst_layer_d,
            const memory_desc_wrapper &dst_iter_d,
            const memory_desc_wrapper &dst_iter_c_d,
            const memory_desc_wrapper &diff_src_layer_d,
            const memory_desc_wrapper &diff_src_iter_d,
            const memory_desc_wrapper &diff_src_iter_c_d,
            const memory_desc_wrapper &diff_weights_layer_d,
            const memory_desc_wrapper &diff_weights_iter_d,
            const memory_desc_wrapper &diff_bias_d,
            const memory_desc_wrapper &diff_dst_layer_d,
            const memory_desc_wrapper &diff_dst_iter_d,
            const memory_desc_wrapper &diff_dst_iter_c_d,
            const memory_desc_wrapper &ws_d, jit_rnn_offsets &jit_off,
            const primitive_attr_t &attr) {

        using namespace rnn_utils;

        jrnn.src_dt = src_layer_d.data_type();
        jrnn.wei_dt = weights_layer_d.data_type();

        jrnn.is_fwd = rnn_conf.is_fwd;
        jrnn.n_layer = rnn_conf.n_layer;
        jrnn.n_dir = rnn_conf.n_dir;
        jrnn.n_iter = rnn_conf.n_iter;
        jrnn.n_gates = rnn_conf.n_gates;
        jrnn.n_bias = rnn_conf.n_bias;
        jrnn.n_states = rnn_conf.n_states;
        jrnn.n_weights_input = weights_layer_d.dims()[2];
        jrnn.n_weights_state = weights_iter_d.dims()[2];
        jrnn.batch = rnn_conf.mb;
        jrnn.slc = rnn_conf.slc;
        jrnn.sic = rnn_conf.sic;
        jrnn.dic = rnn_conf.dic;
        jrnn.dlc = rnn_conf.dlc;
        jrnn.wic = nstl::max(jrnn.slc, nstl::max(jrnn.sic, jrnn.dic));

        jrnn.n_parts_weights_iter = rnn_conf.n_parts_weights_iter;
        jrnn.n_parts_weights_layer = rnn_conf.n_parts_weights_layer;

        jrnn.with_bias = rnn_pd->with_bias();
        jrnn.with_src_iter = rnn_pd->with_src_iter();
        jrnn.with_src_iter_c = rnn_pd->with_src_iter_c();
        jrnn.with_dst_iter = rnn_pd->with_dst_iter();
        jrnn.with_dst_iter_c = rnn_pd->with_dst_iter_c();
        jrnn.is_lbr = rnn_conf.is_lbr;

        jrnn.states_ws_ld = rnn_conf.states_ws_ld;
        jrnn.gates_ws_ld = rnn_conf.gates_ws_ld;

        jrnn.src_layer_ndims = src_layer_d.ndims();
        jrnn.src_iter_ndims = src_iter_d.ndims();
        if (jrnn.with_src_iter_c) jrnn.src_iter_c_ndims = src_iter_c_d.ndims();
        jrnn.weights_layer_ndims = weights_layer_d.ndims();
        jrnn.weights_iter_ndims = weights_iter_d.ndims();
        jrnn.dst_layer_ndims = dst_layer_d.ndims();
        jrnn.dst_iter_ndims = dst_iter_d.ndims();
        if (jrnn.with_dst_iter_c) jrnn.dst_iter_c_ndims = dst_iter_c_d.ndims();
        jrnn.bias_ndims = bias_d.ndims();

        set_offsets(src_layer_d, jit_off.src_layer_off);
        set_offsets(src_iter_d, jit_off.src_iter_off);
        if (jrnn.with_src_iter_c)
            set_offsets(src_iter_c_d, jit_off.src_iter_c_off);
        set_offsets(weights_layer_d, jit_off.weights_layer_off);
        set_offsets(weights_iter_d, jit_off.weights_iter_off);
        set_offsets(bias_d, jit_off.bias_off);
        set_offsets(dst_layer_d, jit_off.dst_layer_off);
        set_offsets(dst_iter_d, jit_off.dst_iter_off);
        if (jrnn.with_dst_iter_c)
            set_offsets(dst_iter_c_d, jit_off.dst_iter_c_off);

        if (!jrnn.is_fwd) {
            jrnn.diff_src_layer_ndims = diff_src_layer_d.ndims();
            jrnn.diff_src_iter_ndims = diff_src_iter_d.ndims();
            if (jrnn.with_src_iter_c)
                jrnn.diff_src_iter_c_ndims = diff_src_iter_c_d.ndims();
            jrnn.diff_weights_layer_ndims = diff_weights_layer_d.ndims();
            jrnn.diff_weights_iter_ndims = diff_weights_iter_d.ndims();
            jrnn.diff_dst_layer_ndims = diff_dst_layer_d.ndims();
            jrnn.diff_dst_iter_ndims = diff_dst_iter_d.ndims();
            if (jrnn.with_dst_iter_c)
                jrnn.diff_dst_iter_c_ndims = diff_dst_iter_c_d.ndims();
            jrnn.diff_bias_ndims = diff_bias_d.ndims();

            set_offsets(diff_src_layer_d, jit_off.diff_src_layer_off);
            set_offsets(diff_src_iter_d, jit_off.diff_src_iter_off);
            if (jrnn.with_src_iter_c)
                set_offsets(diff_src_iter_c_d, jit_off.diff_src_iter_c_off);
            set_offsets(diff_weights_layer_d, jit_off.diff_weights_layer_off);
            set_offsets(diff_weights_iter_d, jit_off.diff_weights_iter_off);
            set_offsets(diff_bias_d, jit_off.diff_bias_off);
            set_offsets(diff_dst_layer_d, jit_off.diff_dst_layer_off);
            set_offsets(diff_dst_iter_d, jit_off.diff_dst_iter_off);
            if (jrnn.with_dst_iter_c)
                set_offsets(diff_dst_iter_c_d, jit_off.diff_dst_iter_c_off);
        }

        rnn_utils::set_offsets(rnn_conf, jrnn.ws_gates_offset,
                jrnn.ws_states_offset, jrnn.ws_c_state_offset,
                jrnn.ws_diff_states_offset, jrnn.ws_grid_comp_offset,
                jrnn.ws_cell_comp_offset, jrnn.ws_bias_offset,
                jrnn.scratchpad_size, jrnn.workspace_size);

        jrnn.cell_kind = rnn_pd->cell_kind();
        jrnn.activation_kind = rnn_pd->activation_kind();
        jrnn.direction_kind = rnn_pd->direction();

        return status::success;
    };

    static status_t init_const_def(compute::kernel_ctx_t &kernel_ctx,
            const jit_rnn_conf_t &jrnn, const jit_rnn_offsets &jit_off) {

        kernel_ctx.set_data_type(jrnn.src_dt);

        kernel_ctx.define_int("IS_FWD", jrnn.is_fwd);
        kernel_ctx.define_int("WITH_BIAS", jrnn.with_bias);
        kernel_ctx.define_int("WITH_SRC_ITER", jrnn.with_src_iter);
        kernel_ctx.define_int("WITH_SRC_ITER_C", jrnn.with_src_iter_c);
        kernel_ctx.define_int("WITH_DST_ITER", jrnn.with_dst_iter);
        kernel_ctx.define_int("WITH_DST_ITER_C", jrnn.with_dst_iter_c);
        kernel_ctx.define_int("IS_LBR", jrnn.is_lbr);

        kernel_ctx.define_int("VANILLA_RNN", alg_kind::vanilla_rnn);
        kernel_ctx.define_int("VANILLA_LSTM", alg_kind::vanilla_lstm);
        kernel_ctx.define_int("VANILLA_GRU", alg_kind::vanilla_gru);
        kernel_ctx.define_int("LBR_GRU", alg_kind::lbr_gru);
        kernel_ctx.define_int("CELL_KIND", jrnn.cell_kind);

        kernel_ctx.define_int("ELTWISE_RELU", alg_kind::eltwise_relu);
        kernel_ctx.define_int("ELTWISE_TANH", alg_kind::eltwise_tanh);
        kernel_ctx.define_int("ELTWISE_LOGISTIC", alg_kind::eltwise_logistic);
        kernel_ctx.define_int("ACTIVATION_KIND", jrnn.activation_kind);

        kernel_ctx.define_int("L2R", dnnl_unidirectional_left2right);
        kernel_ctx.define_int("R2L", dnnl_unidirectional_right2left);
        kernel_ctx.define_int("CONCAT", dnnl_bidirectional_concat);
        kernel_ctx.define_int("SUM", dnnl_bidirectional_sum);
        kernel_ctx.define_int("UNIDEF", dnnl_unidirectional);
        kernel_ctx.define_int("DIRECTION_KIND", jrnn.direction_kind);

        kernel_ctx.define_int("BATCH", jrnn.batch);
        kernel_ctx.define_int("N_DIR", jrnn.n_dir);
        kernel_ctx.define_int("N_LAYER", jrnn.n_layer);
        kernel_ctx.define_int("N_ITER", jrnn.n_iter);
        kernel_ctx.define_int("N_GATES", jrnn.n_gates);
        kernel_ctx.define_int("N_BIAS", jrnn.n_bias);
        kernel_ctx.define_int("N_STATES", jrnn.n_states);

        kernel_ctx.define_int("SLC", jrnn.slc);
        kernel_ctx.define_int("SIC", jrnn.sic);
        kernel_ctx.define_int("DIC", jrnn.dic);
        kernel_ctx.define_int("WIC", jrnn.wic);

        kernel_ctx.define_int("N_PARTS_WEI_ST", jrnn.n_parts_weights_iter);
        kernel_ctx.define_int("N_PARTS_WEI_I", jrnn.n_parts_weights_layer);

        def_offsets(jit_off.src_layer_off, kernel_ctx, "SRC_L",
                jrnn.src_layer_ndims);
        def_offsets(
                jit_off.src_iter_off, kernel_ctx, "SRC_I", jrnn.src_iter_ndims);
        if (jrnn.with_src_iter_c)
            def_offsets(jit_off.src_iter_c_off, kernel_ctx, "SRC_I_C",
                    jrnn.src_iter_c_ndims);
        def_offsets(jit_off.weights_layer_off, kernel_ctx, "WEI_L",
                jrnn.weights_layer_ndims);
        def_offsets(jit_off.weights_iter_off, kernel_ctx, "WEI_I",
                jrnn.weights_iter_ndims);
        def_offsets(jit_off.dst_layer_off, kernel_ctx, "DST_L",
                jrnn.dst_layer_ndims);
        def_offsets(
                jit_off.dst_iter_off, kernel_ctx, "DST_I", jrnn.dst_iter_ndims);
        if (jrnn.with_dst_iter_c)
            def_offsets(jit_off.dst_iter_c_off, kernel_ctx, "DST_I_C",
                    jrnn.dst_iter_c_ndims);
        def_offsets(jit_off.bias_off, kernel_ctx, "BIAS", jrnn.bias_ndims);

        if (!jrnn.is_fwd) {
            def_offsets(jit_off.diff_src_layer_off, kernel_ctx, "DIFF_SRC_L",
                    jrnn.diff_src_layer_ndims);
            def_offsets(jit_off.diff_src_iter_off, kernel_ctx, "DIFF_SRC_I",
                    jrnn.diff_src_iter_ndims);
            if (jrnn.with_src_iter_c)
                def_offsets(jit_off.diff_src_iter_c_off, kernel_ctx,
                        "DIFF_SRC_I_C", jrnn.diff_src_iter_c_ndims);
            def_offsets(jit_off.diff_weights_layer_off, kernel_ctx,
                    "DIFF_WEI_L", jrnn.diff_weights_layer_ndims);
            def_offsets(jit_off.diff_weights_iter_off, kernel_ctx, "DIFF_WEI_I",
                    jrnn.diff_weights_iter_ndims);
            def_offsets(jit_off.diff_dst_layer_off, kernel_ctx, "DIFF_DST_L",
                    jrnn.diff_dst_layer_ndims);
            def_offsets(jit_off.diff_dst_iter_off, kernel_ctx, "DIFF_DST_I",
                    jrnn.diff_dst_iter_ndims);
            if (jrnn.with_dst_iter_c)
                def_offsets(jit_off.diff_dst_iter_c_off, kernel_ctx,
                        "DIFF_DST_I_C", jrnn.diff_dst_iter_c_ndims);
            def_offsets(jit_off.diff_bias_off, kernel_ctx, "DIFF_BIAS",
                    jrnn.diff_bias_ndims);
        }

        kernel_ctx.define_int("WS_GATES_OFFSET", jrnn.ws_gates_offset);
        kernel_ctx.define_int("WS_STATES_OFFSET", jrnn.ws_states_offset);
        kernel_ctx.define_int("WS_C_STATE_OFFSET", jrnn.ws_c_state_offset);
        kernel_ctx.define_int(
                "WS_DIFF_STATES_OFFSET", jrnn.ws_diff_states_offset);
        kernel_ctx.define_int("WS_GRID_COMP_OFFSET", jrnn.ws_grid_comp_offset);
        kernel_ctx.define_int("WS_CELL_COMP_OFFSET", jrnn.ws_cell_comp_offset);
        kernel_ctx.define_int("STATES_WS_LD", jrnn.states_ws_ld);
        kernel_ctx.define_int("GATES_WS_LD", jrnn.gates_ws_ld);
        kernel_ctx.define_int("DEBUGPRINT", DEBUGPRINT);

        DPRINT("\njit_ref_rnn_fwd_kernel: kernel options:\n%s\n\n",
                kernel_ctx.options());

        return status::success;
    }
    jit_rnn_conf_t jrnn;
};

template <prop_kind_t aprop>
inline status_t init_jit(jit_rnn_conf_t &jrnn,
        const rnn_utils::rnn_conf_t &rnn_conf, const rnn_pd_t *rnn_pd,
        jit_rnn_offsets &jit_off) {

    const memory_desc_wrapper fakedesc = rnn_pd->src_md(0);
    return jit_ref_rnn_kernel::init_conf(jrnn, rnn_conf, rnn_pd,
            rnn_pd->src_md(0), rnn_pd->src_md(1), rnn_pd->src_md(2),
            rnn_pd->weights_md(0), rnn_pd->weights_md(1), rnn_pd->weights_md(2),
            rnn_pd->dst_md(0), rnn_pd->dst_md(1), rnn_pd->dst_md(2), fakedesc,
            fakedesc, fakedesc, fakedesc, fakedesc, fakedesc, fakedesc,
            fakedesc, fakedesc, rnn_pd->workspace_md(0), jit_off,
            *rnn_pd->attr());
}

template <>
inline status_t init_jit<prop_kind::backward>(jit_rnn_conf_t &jrnn,
        const rnn_utils::rnn_conf_t &rnn_conf, const rnn_pd_t *rnn_pd,
        jit_rnn_offsets &jit_off) {
    return jit_ref_rnn_kernel::init_conf(jrnn, rnn_conf, rnn_pd,
            rnn_pd->src_md(0), rnn_pd->src_md(1), rnn_pd->src_md(2),
            rnn_pd->weights_md(0), rnn_pd->weights_md(1), rnn_pd->weights_md(2),
            rnn_pd->dst_md(0), rnn_pd->dst_md(1), rnn_pd->dst_md(2),
            rnn_pd->diff_src_md(0), rnn_pd->diff_src_md(1),
            rnn_pd->diff_src_md(2), rnn_pd->diff_weights_md(0),
            rnn_pd->diff_weights_md(1), rnn_pd->diff_weights_md(2),
            rnn_pd->diff_dst_md(0), rnn_pd->diff_dst_md(1),
            rnn_pd->diff_dst_md(2), rnn_pd->workspace_md(0), jit_off,
            *rnn_pd->attr());
}

} // namespace ocl
} // namespace impl
} // namespace dnnl

#endif
