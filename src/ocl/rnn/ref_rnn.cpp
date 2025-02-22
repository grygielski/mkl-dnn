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

/*
  General architecture

  for diff states, we have n_states + 1 as we have n_states diff
  to propagate to the previous iteration and 1 states to propagate
  to the previous layer
  index 0 is dh for cell(t-1, l) to consume
  index 1 is dc for cell(t-1, l) to consume
  index 2 is dh for cell(t, l-1) to consume
  this indexing enables to have the same indexing for states in elemwise
  function
  only the cell execution function should be impacted

 */
#include "ocl/rnn/ref_rnn.hpp"
#include "c_types_map.hpp"
#include "dnnl_thread.hpp"
#include "dnnl_traits.hpp"
#include "math_utils.hpp"
#include "type_helpers.hpp"

namespace dnnl {
namespace impl {
namespace ocl {

using namespace dnnl::impl::utils;
using namespace dnnl::impl::math;
using namespace prop_kind;
using namespace alg_kind;
using namespace rnn_utils;

#define AOC array_offset_calculator

template <prop_kind_t aprop, data_type_t src_type, data_type_t weights_type>
gemm_sig((_ref_rnn_common_t<aprop, src_type, weights_type>::packed_gemm)) {
#if USE_MKL_PACKED_GEMM
// TBD
#else
    UNUSED(m);
    UNUSED(n);
    UNUSED(k);
    UNUSED(a);
    UNUSED(b);
    UNUSED(c);
    UNUSED(is_B_trans);
    UNUSED(beta);
    UNUSED(gemm_kind);
    assert(!"packed gemm is disabled");
#endif
}

template <prop_kind_t aprop, data_type_t src_type, data_type_t weights_type>
gemm_sig((_ref_rnn_common_t<aprop, src_type, weights_type>::gemm_primitive)) {

    // FIXME: This should be created once per execute() instead of creating
    // memory before each gemm call. Each cell type (+prop kind) might have
    // different number of GEMMs.

    memory_desc_t scratchpad_md;

    size_t scratchpad_sz {0}, ws_sz {0};
    get_scratchpad_and_workspace_sizes(pd()->rnn_conf_, scratchpad_sz, ws_sz);
    dims_t scratchpad_dims = {(int)scratchpad_sz};
    dnnl_memory_desc_init_by_tag(
            &scratchpad_md, 1, scratchpad_dims, src_type, format_tag::x);

    void *mem_storage_scratchpad = nullptr;

    memory_t *workspace = (aprop == prop_kind::forward)
            ? ctx.output(DNNL_ARG_WORKSPACE)
            : ctx.input(DNNL_ARG_WORKSPACE);

    if (pd()->rnn_conf_.use_workspace) {
        workspace->memory_storage()->get_data_handle(&mem_storage_scratchpad);
    } else {
        scratchpad_->get_data_handle(&mem_storage_scratchpad);
    }
    using src_t = typename prec_traits<src_type>::type;
    using wei_t = typename prec_traits<weights_type>::type;
    void *mem_storage_weights = nullptr;
    memory_t *weights = nullptr;
    exec_args_t gemm_args;

    std::shared_ptr<memory_t> gemm_mem_A;
    std::shared_ptr<memory_t> gemm_mem_B;
    std::shared_ptr<memory_t> gemm_mem_C;

    switch (gemm_kind) {
        case gemm_iter:
        case gemm_layer:
            weights = (gemm_kind == gemm_layer)
                    ? ctx.input(DNNL_ARG_WEIGHTS_LAYER)
                    : ctx.input(DNNL_ARG_WEIGHTS_ITER);
            weights->memory_storage()->get_data_handle(&mem_storage_weights);

            gemm_mem_A.reset(new memory_t(engine(), weights->md(),
                    memory_flags_t::use_backend_ptr, nullptr));
            gemm_mem_B.reset(new memory_t(engine(), &scratchpad_md,
                    memory_flags_t::use_backend_ptr, nullptr));
            gemm_mem_C.reset(new memory_t(engine(), &scratchpad_md,
                    memory_flags_t::use_backend_ptr, nullptr));
            gemm_mem_A->set_data_handle(mem_storage_weights);
            gemm_mem_B->set_data_handle(mem_storage_scratchpad);
            gemm_mem_C->set_data_handle(mem_storage_scratchpad);
            gemm_mem_A->memory_storage()->set_offset(off_a * sizeof(wei_t));
            gemm_mem_B->memory_storage()->set_offset(off_b * sizeof(src_t));
            gemm_mem_C->memory_storage()->set_offset(off_c * sizeof(src_t));
            break;
        case gemm_diff_wei_iter:
        case gemm_diff_wei_layer:
            weights = (gemm_kind == gemm_diff_wei_iter)
                    ? ctx.output(DNNL_ARG_DIFF_WEIGHTS_ITER)
                    : ctx.output(DNNL_ARG_DIFF_WEIGHTS_LAYER);
            weights->memory_storage()->get_data_handle(&mem_storage_weights);
            gemm_mem_A.reset(new memory_t(engine(), &scratchpad_md,
                    memory_flags_t::use_backend_ptr, nullptr));
            gemm_mem_B.reset(new memory_t(engine(), &scratchpad_md,
                    memory_flags_t::use_backend_ptr, nullptr));
            gemm_mem_C.reset(new memory_t(engine(), weights->md(),
                    memory_flags_t::use_backend_ptr, nullptr));
            gemm_mem_A->set_data_handle(mem_storage_scratchpad);
            gemm_mem_B->set_data_handle(mem_storage_scratchpad);
            gemm_mem_C->set_data_handle(mem_storage_weights);
            gemm_mem_A->memory_storage()->set_offset(off_a * sizeof(src_t));
            gemm_mem_B->memory_storage()->set_offset(off_b * sizeof(src_t));
            gemm_mem_C->memory_storage()->set_offset(off_c * sizeof(wei_t));
            break;
        default: assert(!"unknown gemm_kind");
    }

    gemm_args[DNNL_ARG_SRC_0] = {gemm_mem_A.get(), true};
    gemm_args[DNNL_ARG_SRC_1] = {gemm_mem_B.get(), true};
    gemm_args[DNNL_ARG_DST] = {gemm_mem_C.get(), false};

    auto gemm_ctx = exec_ctx_t(ctx.stream(), std::move(gemm_args));

    switch (gemm_kind) {
        case gemm_iter: gemm_iter_->execute(gemm_ctx); break;
        case gemm_layer: gemm_layer_->execute(gemm_ctx); break;
        case gemm_diff_wei_iter: gemm_diff_wei_iter_->execute(gemm_ctx); break;
        case gemm_diff_wei_layer:
            gemm_diff_wei_layer_->execute(gemm_ctx);
            break;
        default: assert(!"unknown gemm_kind");
    }
}

template <prop_kind_t aprop, data_type_t src_type, data_type_t weights_type>
void _ref_rnn_common_t<aprop, src_type, weights_type>::gates_reduction(
        const exec_ctx_t &ctx, int dir, int lay, int iter, int n_gates, int dic,
        int batch, const memory_storage_t &ws,
        const memory_storage_t &diff_bias) const {
    auto *compute_stream
            = utils::downcast<compute::compute_stream_t *>(ctx.stream());

    compute::kernel_arg_list_t arg_list;
    arg_list.set(0, dir);
    arg_list.set(1, lay);
    arg_list.set(2, iter);
    arg_list.set(3, diff_bias);
    arg_list.set(4, ws);

    auto nd_range = compute::nd_range_t({n_gates, dic});
    compute_stream->parallel_for(nd_range, gates_reduction_kernel_, arg_list);
}

//*************** Grid computations strategy: linear ***************//
template <prop_kind_t aprop, data_type_t src_type, data_type_t weights_type>
grid_execution_sig(
        (_ref_rnn_common_t<aprop, src_type, weights_type>::linear_execution)) {
    // We run the grid of computation
    for (int dir = 0; dir < n_dir; dir++) {
        for (int j = 0; j < n_layer; j++) {
            for (int i = 0; i < n_iter; i++) {
                int lay, iter;
                if (aprop == prop_kind::forward) {
                    lay = j;
                    iter = i;
                } else { // backward
                    lay = n_layer - j - 1;
                    iter = n_iter - i - 1;
                }
                (this->*cell_func)(ctx, dir, lay, iter, dic, slc, sic, wic,
                        batch, n_layer, n_dir, n_iter, n_gates, n_states,
                        n_bias, offset_wei_input_, n_parts_weights_layer,
                        offset_wei_state_, n_parts_weights_iter, bias,
                        workspace, w_input, w_state, diff_weights_layer,
                        diff_weights_iter, diff_bias);
            }
        }
    }
}

//********* GRID computations strategy: utility functions **********//

template <prop_kind_t aprop, data_type_t src_type, data_type_t weights_type>
void _ref_rnn_common_t<aprop, src_type, weights_type>::copy_init_layer(
        compute::compute_stream_t *compute_stream, bool lr, bool rl, int n_iter,
        int batch, int slc, const memory_storage_t &ws,
        const memory_storage_t &input,
        const memory_storage_t &diff_dst_layer) const {

    if (aprop == prop_kind::forward) {
        compute::kernel_arg_list_t arg_list;
        arg_list.set(0, ws);
        arg_list.set(1, input);
        arg_list.set(2, (cl_int)lr);
        arg_list.set(3, (cl_int)rl);
        compute_stream->parallel_for(compute::nd_range_t({slc, batch, n_iter}),
                copy_init_layer_kernel_, arg_list);
    } else {
        compute::kernel_arg_list_t arg_list;
        arg_list.set(0, ws);
        arg_list.set(1, diff_dst_layer);
        arg_list.set(2, (cl_int)0);
        arg_list.set(3, (cl_int)0);
        compute_stream->parallel_for(compute::nd_range_t({batch, n_iter}),
                copy_init_layer_kernel_, arg_list);
    }
}

template <prop_kind_t aprop, data_type_t src_type, data_type_t weights_type>
void _ref_rnn_common_t<aprop, src_type, weights_type>::copy_init_iter(
        compute::compute_stream_t *compute_stream, int n_layer, int n_dir,
        int batch, int sic, int dic, const memory_storage_t &ws,
        const memory_storage_t &firstit_states,
        const memory_storage_t &firstit_c_states,
        const memory_storage_t &diff_dst_iter,
        const memory_storage_t &diff_dst_iter_c) const {

    if (aprop == prop_kind::forward) {
        compute::kernel_arg_list_t arg_list;
        arg_list.set(0, ws);
        arg_list.set(1, firstit_states);
        arg_list.set(2, firstit_c_states);
        compute_stream->parallel_for(
                compute::nd_range_t({sic, batch, n_layer * n_dir}),
                copy_init_iter_kernel_, arg_list);
    } else {
        compute::kernel_arg_list_t arg_list;
        arg_list.set(0, ws);
        arg_list.set(1, diff_dst_iter);
        arg_list.set(2, diff_dst_iter_c);
        compute_stream->parallel_for(
                compute::nd_range_t({dic, batch, n_layer * n_dir}),
                copy_init_iter_kernel_, arg_list);
    }
}

template <prop_kind_t aprop, data_type_t src_type, data_type_t weights_type>
void _ref_rnn_common_t<aprop, src_type, weights_type>::copy_res_layer(
        compute::compute_stream_t *compute_stream, bool lr, bool rl, int n_iter,
        int batch, int slc, int dic, const memory_storage_t &dst_last_layer,
        const memory_storage_t &diff_src_layer,
        const memory_storage_t &ws) const {

    if (aprop == prop_kind::forward) {
        compute::kernel_arg_list_t arg_list;
        arg_list.set(0, ws);
        arg_list.set(1, dst_last_layer);
        arg_list.set(2, (cl_int)lr);
        arg_list.set(3, (cl_int)rl);
        compute_stream->parallel_for(compute::nd_range_t({dic, batch, n_iter}),
                copy_res_layer_kernel_, arg_list);
    } else {
        compute::kernel_arg_list_t arg_list;
        arg_list.set(0, ws);
        arg_list.set(1, diff_src_layer);
        arg_list.set(2, (cl_int)lr);
        arg_list.set(3, (cl_int)rl);
        compute_stream->parallel_for(compute::nd_range_t({slc, batch, n_iter}),
                copy_res_layer_kernel_, arg_list);
    }
}

template <prop_kind_t aprop, data_type_t src_type, data_type_t weights_type>
void _ref_rnn_common_t<aprop, src_type, weights_type>::copy_res_iter(
        compute::compute_stream_t *compute_stream, int n_layer, int n_dir,
        int batch, int sic, int dic, const memory_storage_t &dst_last_iter,
        const memory_storage_t &dst_last_iter_c,
        const memory_storage_t &diff_src_iter,
        const memory_storage_t &diff_src_iter_c,
        const memory_storage_t &ws) const {

    if (aprop == prop_kind::forward) {
        compute::kernel_arg_list_t arg_list;
        arg_list.set(0, ws);
        arg_list.set(1, dst_last_iter);
        arg_list.set(2, dst_last_iter_c);
        compute_stream->parallel_for(
                compute::nd_range_t({dic, batch, n_layer * n_dir}),
                copy_res_iter_kernel_, arg_list);
    } else {
        compute::kernel_arg_list_t arg_list;
        arg_list.set(0, ws);
        arg_list.set(1, diff_src_iter);
        arg_list.set(2, diff_src_iter_c);
        compute_stream->parallel_for(
                compute::nd_range_t({sic, batch, n_layer * n_dir}),
                copy_res_iter_kernel_, arg_list);
    }
}

template <prop_kind_t aprop, data_type_t src_type, data_type_t weights_type>
void _ref_rnn_common_t<aprop, src_type, weights_type>::ws_set(
        compute::compute_stream_t *compute_stream,
        const memory_storage_t &workspace_, const cl_ulong ws_offset,
        const float val, const size_t size) const {

    compute::kernel_arg_list_t arg_list;
    arg_list.set(0, workspace_);
    arg_list.set(1, ws_offset);
    arg_list.set(2, val);
    auto nd_range = compute::nd_range_t({size});
    compute_stream->parallel_for(nd_range, ws_set_kernel_, arg_list);
}

#if DEBUGPRINT
template <prop_kind_t aprop, data_type_t src_type, data_type_t weights_type>
void _ref_rnn_common_t<aprop, src_type, weights_type>::ws_print(
        compute::compute_stream_t *compute_stream,
        const memory_storage_t &workspace_) const {

    compute::kernel_arg_list_t arg_list;
    arg_list.set(0, workspace_);
    auto nd_range = compute::nd_range_t({1});
    compute_stream->parallel_for(nd_range, ws_print_kernel_, arg_list);
}
#endif

template <prop_kind_t aprop, data_type_t src_type, data_type_t weights_type>
packing_sig((_ref_rnn_common_t<aprop, src_type, weights_type>::pack_weights)) {
#if USE_MKL_PACKED_GEMM
// TBD
#else
    UNUSED(n_layer);
    UNUSED(n_dir);
    UNUSED(n_weights);
    UNUSED(n_gates);
    UNUSED(n_parts);
    UNUSED(gates_per_part);
    UNUSED(batch);
    UNUSED(OC_size);
    UNUSED(IC_size);
    UNUSED(weights_);
    UNUSED(w_);
    assert(!"packed gemm is disabled");
#endif
}

template <prop_kind_t aprop, data_type_t src_type, data_type_t weights_type>
packing_sig(
        (_ref_rnn_common_t<aprop, src_type, weights_type>::no_pack_weights)) {
    AOC<size_t, 3> weights(weights_, n_layer, n_dir, n_parts);

    for (int i = 0; i < n_layer; i++) {
        for (int d = 0; d < n_dir; d++) {
            weights(i, d, 0) = OFF5(
                    i, n_layer, d, n_dir, 0, OC_size, 0, n_gates, 0, IC_size);
            for (int p = 1; p < n_parts; p++) {
                weights(i, d, p) = OFF5(i, n_layer, d, n_dir, 0, OC_size,
                        gates_per_part[p - 1], n_gates, 0, IC_size);
            }
        }
    }
}
template <prop_kind_t aprop, data_type_t src_type, data_type_t weights_type>
free_packed_sig((_ref_rnn_common_t<aprop, src_type,
        weights_type>::free_packed_weights)) {
#if USE_MKL_PACKED_GEMM
// TBD
#else
    UNUSED(n_layer);
    UNUSED(n_dir);
    UNUSED(n_parts);
    UNUSED(weights_);
    assert(!"packed gemm is disabled");
#endif
}
template <prop_kind_t aprop, data_type_t src_type, data_type_t weights_type>
free_packed_sig((_ref_rnn_common_t<aprop, src_type,
        weights_type>::free_no_packed_weights)) {
    UNUSED(n_layer);
    UNUSED(n_dir);
    UNUSED(n_parts);
    UNUSED(weights_);
}

//********************* Execution function *********************//

template <prop_kind_t aprop, data_type_t src_type, data_type_t weights_type>
status_t _ref_rnn_common_t<aprop, src_type, weights_type>::execute_(
        const exec_ctx_t &ctx) const {

    auto *compute_stream
            = utils::downcast<compute::compute_stream_t *>(ctx.stream());

    auto rnn = this->pd();
    const rnn_conf_t &rnn_conf = this->pd()->rnn_conf_;

    int n_layer = rnn_conf.n_layer;

    int n_dir = rnn_conf.n_dir;
    int n_iter = rnn_conf.n_iter;
    int n_gates = rnn_conf.n_gates;
    int n_bias = rnn_conf.n_bias;
    int n_states = rnn_conf.n_states;
    int n_weights_input = rnn->SLC();
    int n_weights_state = rnn->SIC();
    int batch = rnn_conf.mb;
    int slc = rnn_conf.slc;
    int sic = rnn_conf.sic;
    int dic = rnn_conf.dic;
    int dlc = rnn_conf.dlc;
    int wic = nstl::max(slc, nstl::max(sic, dic));

    bool is_orig_gru = rnn->cell_kind() == alg_kind::vanilla_gru;
    int n_parts_weights_iter = rnn_conf.n_parts_weights_iter;
    int n_parts_weights_layer = rnn_conf.n_parts_weights_layer;

    bool is_fwd = rnn_conf.is_fwd;

    auto &input_native_ = CTX_IN_STORAGE(DNNL_ARG_SRC_LAYER);
    auto &states_native_ = CTX_IN_STORAGE(DNNL_ARG_SRC_ITER);
    auto &c_states_native_ = CTX_IN_STORAGE(DNNL_ARG_SRC_ITER_C);
    auto &w_input_native_ = CTX_IN_STORAGE(DNNL_ARG_WEIGHTS_LAYER);
    auto &w_state_native_ = CTX_IN_STORAGE(DNNL_ARG_WEIGHTS_ITER);
    auto &bias_native_ = CTX_IN_STORAGE(DNNL_ARG_BIAS);

    auto &dst_last_layer_native_ = is_fwd ? CTX_OUT_STORAGE(DNNL_ARG_DST_LAYER)
                                          : CTX_IN_STORAGE(DNNL_ARG_DST_LAYER);
    auto &dst_last_iter_native_ = is_fwd ? CTX_OUT_STORAGE(DNNL_ARG_DST_ITER)
                                         : CTX_IN_STORAGE(DNNL_ARG_DST_ITER);
    auto &dst_last_iter_c_native_ = is_fwd
            ? CTX_OUT_STORAGE(DNNL_ARG_DST_ITER_C)
            : CTX_IN_STORAGE(DNNL_ARG_DST_ITER_C);

    auto &diff_dst_layer_native_ = CTX_IN_STORAGE(DNNL_ARG_DIFF_DST_LAYER);
    auto &diff_dst_iter_native_ = CTX_IN_STORAGE(DNNL_ARG_DIFF_DST_ITER);
    auto &diff_dst_iter_c_native_ = CTX_IN_STORAGE(DNNL_ARG_DIFF_DST_ITER_C);

    auto &workspace_ = rnn_conf.is_training
            ? is_fwd ? CTX_OUT_STORAGE(DNNL_ARG_WORKSPACE)
                     : CTX_IN_STORAGE(DNNL_ARG_WORKSPACE)
#if !EMULATED_SCRATCHPAD
            : CTX_IN_STORAGE(DNNL_ARG_SCRATCHPAD);
#else
            : *scratchpad_;
#endif

    auto &diff_src_layer_native_ = CTX_OUT_STORAGE(DNNL_ARG_DIFF_SRC_LAYER);
    auto &diff_src_iter_native_ = CTX_OUT_STORAGE(DNNL_ARG_DIFF_SRC_ITER);
    auto &diff_src_iter_c_native_ = CTX_OUT_STORAGE(DNNL_ARG_DIFF_SRC_ITER_C);

    auto &diff_weights_layer_native_
            = CTX_OUT_STORAGE(DNNL_ARG_DIFF_WEIGHTS_LAYER);
    auto &diff_weights_iter_native_
            = CTX_OUT_STORAGE(DNNL_ARG_DIFF_WEIGHTS_ITER);
    auto &diff_bias_native_ = CTX_OUT_STORAGE(DNNL_ARG_DIFF_BIAS);

    auto prints = [=](void) {
        DPRINT("\n%s\n", "+++++++++++++++");
        DPRINT(" aprop = %d\n", (int)aprop);
        DPRINT("%s\n", "+++++++++++++++");
        DPRINT("  n_layer         = %d\n", n_layer);
        DPRINT("  n_dir           = %d\n", n_dir);
        DPRINT("  n_iter          = %d\n", n_iter);
        DPRINT("  n_gates         = %d\n", n_gates);
        DPRINT("  n_bias          = %d\n", n_bias);
        DPRINT("  n_states        = %d\n", n_states);
        DPRINT("  n_weights_input = %d\n", n_weights_input);
        DPRINT("  n_weights_state = %d\n", n_weights_state);
        DPRINT("  batch           = %d\n", batch);
        DPRINT("  slc             = %d\n", slc);
        DPRINT("  sic             = %d\n", sic);
        DPRINT("  dic             = %d\n", dic);
        DPRINT("  dlc             = %d\n", dlc);
        DPRINT("  wic             = %d\n", wic);
        DPRINT("%s\n", "+++++++++++++++");
        DPRINT("  is_fwd          = %s\n", is_fwd ? "yes" : "no");
        DPRINT("  is_orig_gru     = %s\n", is_orig_gru ? "yes" : "no");
        DPRINT("  use_workspace   = %s\n",
                rnn_conf.use_workspace ? "yes" : "no");
        DPRINT("%s\n", "+++++++++++++++");
        DPRINT("  with_src_iter   = %s\n", rnn->with_src_iter() ? "yes" : "no");
        DPRINT("  with_src_iter_c = %s\n",
                rnn->with_src_iter_c() ? "yes" : "no");
        DPRINT("  with_bias       = %s\n", rnn->with_bias() ? "yes" : "no");
        DPRINT("  with_dst_iter   = %s\n", rnn->with_dst_iter() ? "yes" : "no");
        DPRINT("  with_dst_iter_c = %s\n",
                rnn->with_dst_iter_c() ? "yes" : "no");
        DPRINT("%s\n", "+++++++++++++++");
    };

#if DEBUGPRINT
    prints();
#else
    UNUSED(dlc);
    UNUSED(is_orig_gru);
    UNUSED(prints);
#endif

#if WS_NAN_FILLING
    if (rnn_conf.is_fwd) {
        DPRINT("DEBUG ws NaN filling: (offset, size) states: %ld %ld c_states: "
               "%ld %ld gates: %ld %ld\n",
                ws_states_offset_, rnn_conf.ws_states_size, ws_c_states_offset_,
                rnn_conf.ws_c_states_size, ws_gates_offset_,
                rnn_conf.ws_gates_size);
        ws_set(compute_stream, workspace_, ws_states_offset_, NAN,
                rnn_conf.ws_states_size);
        if (rnn->with_src_iter_c()) {
            ws_set(compute_stream, workspace_, ws_c_states_offset_, NAN,
                    rnn_conf.ws_c_states_size);
        }
        ws_set(compute_stream, workspace_, ws_gates_offset_, NAN,
                rnn_conf.ws_gates_size);
    }
#endif

    // initialize diff_state to 0
    if (aprop == prop_kind::backward) {
        ws_set(compute_stream, workspace_, ws_diff_states_offset_, 0.0f,
                rnn_conf.ws_diff_states_size);
    }

    // TODO: implement without copies
    bool is_lr = !one_of(rnn_conf.exec_dir, b2t_r2l, t2b_r2l);
    bool is_rl = !one_of(rnn_conf.exec_dir, b2t_l2r, t2b_l2r);

    // XXX: this function is used for calculating offsets for buffers and not
    // used for packing weights
    (this->*weights_state_pack_func)(n_layer, n_dir, n_weights_state, n_gates,
            batch, dic, sic, offset_wei_state_, n_parts_weights_iter,
            rnn_conf.parts_weights_iter, w_state_native_);
    (this->*weights_input_pack_func)(n_layer, n_dir, n_weights_input, n_gates,
            batch, dic, slc, offset_wei_input_, n_parts_weights_layer,
            rnn_conf.parts_weights_layer, w_input_native_);

    DPRINT("\n%s(%d) WS before copy init\n\n", __FUNCTION__, __LINE__);
    WS_PRINT(compute_stream, workspace_);

    // we first need to copy the initial states and input into ws
    copy_init_layer(compute_stream, is_lr, is_rl, n_iter, batch, slc,
            workspace_, input_native_, diff_dst_layer_native_);
    copy_init_iter(compute_stream, n_layer, n_dir, batch, sic, dic, workspace_,
            states_native_, c_states_native_, diff_dst_iter_native_,
            diff_dst_iter_c_native_);

    DPRINT("\n%s(%d) WS before grid\n\n", __FUNCTION__, __LINE__);
    WS_PRINT(compute_stream, workspace_);

    // run the execution on the grid
    (this->*grid_computation)(ctx, dic, slc, sic, wic, batch, n_layer, n_dir,
            n_iter, n_gates, n_states, n_bias, offset_wei_input_,
            n_parts_weights_layer, offset_wei_state_, n_parts_weights_iter,
            bias_native_, workspace_, w_input_native_, w_state_native_,
            diff_weights_layer_native_, diff_weights_iter_native_,
            diff_bias_native_);

    DPRINT("\n%s(%d) WS before copy res\n\n", __FUNCTION__, __LINE__);
    WS_PRINT(compute_stream, workspace_);

    // Finally we copy the results to the result buffers

    copy_res_layer(compute_stream, is_lr, is_rl, n_iter, batch, slc, dic,
            dst_last_layer_native_, diff_src_layer_native_, workspace_);
    copy_res_iter(compute_stream, n_layer, n_dir, batch, sic, dic,
            dst_last_iter_native_, dst_last_iter_c_native_,
            diff_src_iter_native_, diff_src_iter_c_native_, workspace_);

    // NOT USED YET
#if 0
    // We free the packed weights if they were packed internally
    // currently - not
    (this->*weights_state_free_packed_func)(n_layer, n_dir,
            n_parts_weights_iter, offset_wei_state_);
    (this->*weights_input_free_packed_func)(n_layer, n_dir,
            n_parts_weights_layer, offset_wei_input_);
#endif

    return status::success;
};

/* Fix for MSVS warning C4661 */
template <>
cell_execution_sig(ref_rnn_fwd_f32_t::cell_execution);
template <>
cell_execution_sig(ref_rnn_bwd_f32_t::cell_execution);
template <>
cell_execution_sig(ref_rnn_fwd_f32_t::cell_execution_gru);
template <>
cell_execution_sig(ref_rnn_bwd_f32_t::cell_execution_gru);
template <>
cell_execution_sig(ref_rnn_fwd_f32_t::cell_execution_gru_lbr);
template <>
cell_execution_sig(ref_rnn_bwd_f32_t::cell_execution_gru_lbr);
template <>
cell_execution_sig(ref_rnn_fwd_f16_t::cell_execution);
template <>
cell_execution_sig(ref_rnn_fwd_f16_t::cell_execution_gru);
template <>
cell_execution_sig(ref_rnn_fwd_f16_t::cell_execution_gru_lbr);
template <>
elemwise_sig(ref_rnn_fwd_f16_t::rnn_elemwise);
template <>
elemwise_sig(ref_rnn_fwd_f32_t::rnn_elemwise);
template <>
elemwise_sig(ref_rnn_bwd_f32_t::rnn_elemwise);
template <>
elemwise_sig(ref_rnn_fwd_f16_t::lstm_elemwise);
template <>
elemwise_sig(ref_rnn_fwd_f32_t::lstm_elemwise);
template <>
elemwise_sig(ref_rnn_bwd_f32_t::lstm_elemwise);
template <>
elemwise_sig(ref_rnn_fwd_f16_t::gru_lbr_elemwise);
template <>
elemwise_sig(ref_rnn_fwd_f32_t::gru_lbr_elemwise);
template <>
elemwise_sig(ref_rnn_bwd_f32_t::gru_lbr_elemwise);

template struct _ref_rnn_common_t<prop_kind::forward, data_type::f16,
        data_type::f16>;
template struct _ref_rnn_common_t<prop_kind::forward, data_type::f32,
        data_type::f32>;
template struct _ref_rnn_common_t<prop_kind::backward, data_type::f32,
        data_type::f32>;
} // namespace ocl
} // namespace impl
} // namespace dnnl
