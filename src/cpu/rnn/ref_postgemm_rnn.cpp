/*******************************************************************************
* Copyright 2018 Intel Corporation
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
 * Cell execution of Vanilla RNN
 */

#include "dnnl_thread.hpp"
#include "math_utils.hpp"

#include "jit_uni_rnn_common_postgemm_dispatcher.hpp"

namespace dnnl {
namespace impl {
namespace cpu {

using namespace dnnl::impl::utils;
using namespace dnnl::impl::math;
using namespace rnn_utils;

template <>
float activation<alg_kind::eltwise_relu, prop_kind::forward>(
        float s, float alpha, float cliping) {
    return relu_fwd<float>(s, alpha);
}

template <>
float activation<alg_kind::eltwise_relu, prop_kind::backward>(
        float s, float alpha, float cliping) {
    return relu_bwd<float>(s, alpha);
}

template <>
float activation<alg_kind::eltwise_tanh, prop_kind::forward>(
        float s, float alpha, float cliping) {
    return tanh_fwd<float>(s);
}

template <>
float activation<alg_kind::eltwise_tanh, prop_kind::backward>(
        float s, float alpha, float cliping) {
    return one_m_square<float>(s);
}

template <>
float activation<alg_kind::eltwise_logistic, prop_kind::forward>(
        float s, float alpha, float cliping) {
    return logistic_fwd<float>(s);
}

template <>
float activation<alg_kind::eltwise_logistic, prop_kind::backward>(
        float s, float alpha, float cliping) {
    return x_m_square<float>(s);
}

float linear(float s, float alpha, float clipping) {
    return alpha * s;
}

template <typename T, typename acc_data_t, typename src_data_t>
void rnn_postgemm_template(T func1, const float *scales, float alpha,
        const rnn_utils::rnn_conf_t &rnn, acc_data_t *ws_gates_,
        src_data_t *states_t_l_, src_data_t *states_tm1_l_, float *bias_) {

    ws_gates_aoc_t ws_gates(rnn, ws_gates_);
    bias_aoc_t bias(rnn, bias_);
    ws_states_aoc_t states_t_l(rnn, states_t_l_);
    if (scales != nullptr) alpha = scales[0];

    parallel_nd(rnn.mb, [&](int i) {
        for (int j = 0; j < rnn.dic; j++) {
            const float h = func1(ws_gates(i, 0, j) + bias(0, j), alpha, 0);
            ws_gates(i, 0, j) = states_t_l(i, j) = h;
        }
    });
}

template <>
rnn_postgemm_sig(rnn_postgemm_fwd_f32_t::rnn_postgemm) {
    const float *scales = pd_->attr()->rnn_tparams_.scales_;
    auto act_f = [this](float a, float alpha, float clipping) {
        return this->activation_func(a, alpha, clipping);
    };
    auto linear_f = [](float a, float alpha, float clipping) {
        return linear(a, alpha, clipping);
    };
    auto alpha = pd_->desc()->alpha;
    if (!pd_->attr()->rnn_tparams_.test_mode_)
        rnn_postgemm_template(act_f, nullptr, alpha, rnn, ws_gates_,
                states_t_l_, states_tm1_l_, bias_);
    else
        rnn_postgemm_template(linear_f, scales, alpha, rnn, ws_gates_,
                states_t_l_, states_tm1_l_, bias_);
}

template <>
rnn_postgemm_sig(rnn_postgemm_fwd_u8_t::rnn_postgemm) {
    assert(!"VANILLA RNN int8 is not supported");
}

template <typename T, typename acc_data_t>
void rnn_postgemm_template(T func1, const float *scales, float alpha,
        const rnn_utils::rnn_conf_t &rnn, acc_data_t *ws_gates_,
        float *diff_states_tp1_l_, float *diff_states_t_lp1_) {
    ws_gates_aoc_t ws_gates(rnn, ws_gates_);
    ws_diff_states_aoc_t diff_states_tp1_l(rnn, diff_states_tp1_l_);
    ws_diff_states_aoc_t diff_states_t_lp1(rnn, diff_states_t_lp1_);
    if (scales != nullptr) alpha = scales[0];

    parallel_nd(rnn.mb, [&](int i) {
        for (int j = 0; j < rnn.dic; ++j) {
            const float dH = diff_states_t_lp1(rnn.n_states, i, j)
                    + diff_states_tp1_l(0, i, j);
            auto g = ws_gates(i, 0, j);
            ws_gates(i, 0, j) = dH * func1(g, alpha, 0);
        }
    });
}

template <>
rnn_postgemm_sig(rnn_postgemm_bwd_f32_t::rnn_postgemm) {
    const float *scales = pd_->attr()->rnn_tparams_.scales_;
    auto act_f = [this](float a, float alpha, float clipping) {
        return this->activation_func(a, alpha, 0);
    };
    auto linear_f = [](float a, float alpha, float clipping) {
        return linear(a, alpha, 0);
    };
    auto alpha = pd_->desc()->alpha;
    if (!pd_->attr()->rnn_tparams_.test_mode_)
        rnn_postgemm_template(act_f, nullptr, alpha, rnn, ws_gates_,
                diff_states_tp1_l_, diff_states_t_lp1_);
    else
        rnn_postgemm_template(linear_f, scales, alpha, rnn, ws_gates_,
                diff_states_tp1_l_, diff_states_t_lp1_);
}

} // namespace cpu
} // namespace impl
} // namespace dnnl
