/*******************************************************************************
* Copyright 2016-2018 Intel Corporation
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

#include <memory>

#include "dnnl.h"
#include "dnnl_thread.hpp"
#include "engine.hpp"
#include "nstl.hpp"
#include "primitive.hpp"

#include "c_types_map.hpp"
#include "utils.hpp"

#include "cpu/cpu_engine.hpp"

#if DNNL_GPU_RUNTIME == DNNL_RUNTIME_OCL
#include "ocl/ocl_engine.hpp"
#endif

namespace dnnl {
namespace impl {

static inline std::unique_ptr<engine_factory_t> get_engine_factory(
        engine_kind_t kind, backend_kind_t backend_kind) {
    if (kind == engine_kind::cpu && backend_kind == backend_kind::native) {
        return std::unique_ptr<engine_factory_t>(
                new cpu::cpu_engine_factory_t());
    }
#if DNNL_GPU_RUNTIME == DNNL_RUNTIME_OCL
    if (kind == engine_kind::gpu && backend_kind == backend_kind::ocl) {
        return std::unique_ptr<engine_factory_t>(
                new ocl::ocl_engine_factory_t(kind));
    }
#endif
    return nullptr;
}

static inline backend_kind_t get_default_backend(engine_kind_t kind) {
#if DNNL_GPU_RUNTIME == DNNL_RUNTIME_OCL
    if (kind == engine_kind::gpu) return backend_kind::ocl;
#endif
    return backend_kind::native;
}

} // namespace impl
} // namespace dnnl

using namespace dnnl::impl;
using namespace dnnl::impl::status;
using namespace dnnl::impl::utils;

// XXX: allows to have threading related functions in a limited scope
int dnnl_engine::dnnl_get_max_threads() {
    return ::dnnl_get_max_threads();
}

size_t dnnl_engine_get_count(engine_kind_t kind) {
    auto ef = get_engine_factory(kind, get_default_backend(kind));
    return ef != nullptr ? ef->count() : 0;
}

status_t dnnl_engine_create(
        engine_t **engine, engine_kind_t kind, size_t index) {
    if (engine == nullptr) return invalid_arguments;

    auto ef = get_engine_factory(kind, get_default_backend(kind));
    if (ef == nullptr || index >= ef->count()) return invalid_arguments;

    return ef->engine_create(engine, index);
}

extern "C" status_t DNNL_API dnnl_engine_create_with_backend(
        engine_t **engine, engine_kind_t kind, int backend_kind, size_t index) {
    if (engine == nullptr) return invalid_arguments;

    auto ef = get_engine_factory(kind, (backend_kind_t)backend_kind);
    if (ef == nullptr || index >= ef->count()) return invalid_arguments;

    return ef->engine_create(engine, index);
}

status_t dnnl_engine_get_kind(engine_t *engine, engine_kind_t *kind) {
    if (engine == nullptr) return invalid_arguments;
    *kind = engine->kind();
    return success;
}

extern "C" status_t DNNL_API dnnl_engine_get_backend_kind(
        engine_t *engine, backend_kind_t *backend_kind) {
    bool args_ok = !any_null(engine, backend_kind);
    if (!args_ok) return invalid_arguments;

    *backend_kind = engine->backend_kind();
    return success;
}

status_t dnnl_engine_destroy(engine_t *engine) {
    /* TODO: engine->dec_ref_count(); */
    delete engine;
    return success;
}

// vim: et ts=4 sw=4 cindent cino+=l0,\:4,N-s
