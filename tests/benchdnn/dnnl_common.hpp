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

#ifndef DNNL_COMMON_HPP
#define DNNL_COMMON_HPP

#include <stddef.h>
#include <stdint.h>
#include <vector>

#include "dnnl.h"
#include "src/common/bfloat16.hpp"
#include "src/common/float16.hpp"
#include "src/common/nstl.hpp"

#include "common.hpp"
#include "dnn_types.hpp"
#include "dnnl_debug.hpp"

#define for_ for

#define DNN_SAFE(f, s) \
    do { \
        dnnl_status_t status = f; \
        if (status != dnnl_success) { \
            if (s == CRIT || s == WARN) { \
                print(0, "error [%s:%d]: '%s' -> %s(%d)\n", \
                        __PRETTY_FUNCTION__, __LINE__, #f, status2str(status), \
                        (int)status); \
                fflush(0); \
                if (s == CRIT) exit(2); \
            } \
            return FAIL; \
        } \
    } while (0)

#define DNN_SAFE_V(f) \
    do { \
        dnnl_status_t status = f; \
        if (status != dnnl_success) { \
            print(0, "error [%s:%d]: '%s' -> %s(%d)\n", __PRETTY_FUNCTION__, \
                    __LINE__, STRINGIFY(f), status2str(status), (int)status); \
            fflush(0); \
            exit(2); \
        } \
    } while (0)

#define DNN_SAFE_CLEAN(f, s, clean) \
    do { \
        dnnl_status_t status = f; \
        if (status != dnnl_success) { \
            if (s == CRIT || s == WARN) { \
                print(0, "error [%s:%d]: '%s' -> %s(%d)\n", \
                        __PRETTY_FUNCTION__, __LINE__, #f, status2str(status), \
                        (int)status); \
                fflush(0); \
                if (s == CRIT) exit(2); \
            } \
            clean(); \
            return FAIL; \
        } \
    } while (0)

/* aux */
using bfloat16_t = dnnl::impl::bfloat16_t;
using float16_t = dnnl::impl::float16_t;
template <dnnl_data_type_t>
struct prec_traits;
template <>
struct prec_traits<dnnl_bf16> {
    typedef bfloat16_t type;
};
template <>
struct prec_traits<dnnl_f16> {
    typedef float16_t type;
};
template <>
struct prec_traits<dnnl_f32> {
    typedef float type;
};
template <>
struct prec_traits<dnnl_s32> {
    typedef int32_t type;
};
template <>
struct prec_traits<dnnl_s8> {
    typedef int8_t type;
};
template <>
struct prec_traits<dnnl_u8> {
    typedef uint8_t type;
};

#define CASE_ALL(dt) \
    switch (dt) { \
        CASE(dnnl_bf16); \
        CASE(dnnl_f16); \
        CASE(dnnl_f32); \
        CASE(dnnl_s32); \
        CASE(dnnl_s8); \
        CASE(dnnl_u8); \
        default: assert(!"bad data_type"); \
    }

inline size_t sizeof_dt(dnnl_data_type_t dt) {
#define CASE(dt) \
    case dt: return sizeof(typename prec_traits<dt>::type);

    CASE_ALL(dt);

#undef CASE
    return 0;
}

/* std::numeric_limits::digits functionality */
inline int digits_dt(dnnl_data_type_t dt) {
#define CASE(dt) \
    case dt: \
        return dnnl::impl::nstl::numeric_limits< \
                typename prec_traits<dt>::type>::digits;

    CASE_ALL(dt);

#undef CASE
    return 0;
}

#undef CASE_ALL

template <dnnl_data_type_t dt>
inline float saturate(float val) {
    return MAX2(dnnl::impl::nstl::numeric_limits<
                        typename prec_traits<dt>::type>::lowest(),
            MIN2(dnnl::impl::nstl::numeric_limits<
                         typename prec_traits<dt>::type>::max(),
                    mxcsr_round(val)));
}

inline float maybe_saturate(dnnl_data_type_t dt, float value) {
    if (dt == dnnl_s32 || dt == dnnl_s8 || dt == dnnl_u8) {
        switch (dt) {
#define CASE(dt) \
    case dt: { \
        return saturate<dt>(value); \
    }
            CASE(dnnl_s32);
            CASE(dnnl_s8);
            CASE(dnnl_u8);
#undef CASE
            default: assert(!"bad data_type");
        }
        return 0;
    }
    return value;
}

/* simplification */
extern dnnl_engine_kind_t engine_tgt_kind;

extern dnnl_engine_t engine_ref;
extern dnnl_engine_t engine_tgt;

extern dnnl_stream_t stream_ref;
extern dnnl_stream_t stream_tgt;

extern "C" dnnl_status_t dnnl_engine_create_with_backend(dnnl_engine_t *engine,
        dnnl_engine_kind_t kind, int backend_kind, size_t index);
extern "C" dnnl_status_t dnnl_engine_get_backend_kind(
        dnnl_engine_t engine, int *backend_kind);

#if DNNL_GPU_RUNTIME == DNNL_RUNTIME_OCL
extern "C" dnnl_status_t DNNL_API dnnl_impl_gpu_reorder_set_engine_kind(
        dnnl_engine_kind_t engine_kind);
#endif

inline int init() {
    /* Create engine with CPU native backend: backend_kind == 0 */
    DNN_SAFE(
            dnnl_engine_create_with_backend(&engine_ref, dnnl_cpu, 0, 0), CRIT);
    DNN_SAFE(dnnl_stream_create(
                     &stream_ref, engine_ref, dnnl_stream_default_flags),
            CRIT);

    if (!engine_tgt) {
        DNN_SAFE(dnnl_engine_create(&engine_tgt, engine_tgt_kind, 0), CRIT);
        DNN_SAFE(dnnl_stream_create(
                         &stream_tgt, engine_tgt, dnnl_stream_default_flags),
                CRIT);
    }

    // Optimization to reduce testing time for GPU.
    //
    // For CPU <-> GPU reorders, the library creates GPU-side kernels.
    // Benchdnn heavily relies on reorders so this greatly increases execution
    // time because of a big overhead on building OpenCL kernels.
    //
    // This moves all such reorders to CPU to reduce testing time. Reorder, sum
    // and concat primitives are used to test GPU reorders so leave them
    // without changes.
#if DNNL_GPU_RUNTIME == DNNL_RUNTIME_OCL
    std::string driver = std::string(driver_name);
    if (driver != std::string("reorder") && driver != std::string("sum")
            && driver != std::string("concat")) {
        dnnl_impl_gpu_reorder_set_engine_kind(dnnl_cpu);
    }
#endif

    return OK;
}

inline int finalize() {
    DNN_SAFE(dnnl_stream_destroy(stream_ref), CRIT);
    DNN_SAFE(dnnl_stream_destroy(stream_tgt), CRIT);

    DNN_SAFE(dnnl_engine_destroy(engine_ref), CRIT);
    DNN_SAFE(dnnl_engine_destroy(engine_tgt), CRIT);
    return OK;
}

inline const char *query_impl_info(const_dnnl_primitive_desc_t pd) {
    const char *str;
    dnnl_primitive_desc_query(pd, dnnl_query_impl_info_str, 0, &str);
    return str;
}

struct args_t {
    args_t &set(int arg, dnnl_memory_t memory) {
        dnnl_exec_arg_t a = {arg, memory};
        args_.push_back(a);
        return *this;
    }
    void clear() { args_.clear(); }

    int size() const { return (int)args_.size(); }
    const dnnl_exec_arg_t *args() const { return args_.data(); }
    operator const dnnl_exec_arg_t *() const { return args(); }

private:
    std::vector<dnnl_exec_arg_t> args_;
};

inline dnnl_status_t execute_and_wait(dnnl_primitive_t prim,
        dnnl_stream_t stream, int nargs, const dnnl_exec_arg_t *args) {
    dnnl_status_t status = dnnl_primitive_execute(prim, stream, nargs, args);
    if (status != dnnl_success) return status;
    return dnnl_stream_wait(stream);
}

inline int measure_perf(
        benchdnn_timer_t &t, dnnl_primitive_t prim, args_t &args) {
    if (bench_mode & PERF) {
        t.reset();
        while (true) {
            DNN_SAFE(execute_and_wait(prim, stream_tgt, args.size(), args),
                    WARN);
            t.stamp();
            const bool stop = false
                    || (fix_times_per_prb && t.times() >= fix_times_per_prb)
                    || (!fix_times_per_prb && t.total_ms() >= max_ms_per_prb
                            && t.times() >= min_times_per_prb);
            if (stop) break;
        }
    }
    return OK;
}

#endif
