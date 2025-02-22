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

#ifndef EXAMPLE_UTILS_H
#define EXAMPLE_UTILS_H

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dnnl.h"

#define CHECK(f) \
    do { \
        dnnl_status_t s_ = f; \
        if (s_ != dnnl_success) { \
            printf("[%s:%d] error: %s returns %d\n", __FILE__, __LINE__, #f, \
                    s_); \
            exit(2); \
        } \
    } while (0)

#define CHECK_TRUE(expr) \
    do { \
        int e_ = expr; \
        if (!e_) { \
            printf("[%s:%d] %s failed\n", __FILE__, __LINE__, #expr); \
            exit(2); \
        } \
    } while (0)

static dnnl_engine_kind_t parse_engine_kind(int argc, char **argv) {
    // Returns default engine kind, i.e. CPU, if none given
    if (argc == 1) {
        return dnnl_cpu;
    } else if (argc == 2) {
        // Checking the engine type, i.e. CPU or GPU
        char *engine_kind_str = argv[1];
        if (!strcmp(engine_kind_str, "cpu")) {
            return dnnl_cpu;
        } else if (!strcmp(engine_kind_str, "gpu")) {
            // Checking if a GPU exists on the machine
            if (!dnnl_engine_get_count(dnnl_gpu)) {
                fprintf(stderr,
                        "Application couldn't find GPU, please run with CPU "
                        "instead. Thanks!\n");
                exit(1);
            }
            return dnnl_gpu;
        }
    }

    // If all above fails, the example should be ran properly
    fprintf(stderr, "Please run example like this: %s cpu|gpu\n", argv[0]);
    exit(1);
}

// Read from memory, write to handle
static inline void read_from_dnnl_memory(void *handle, dnnl_memory_t mem) {
    dnnl_engine_t eng;
    dnnl_engine_kind_t eng_kind;
    const dnnl_memory_desc_t *md;

    CHECK(dnnl_memory_get_engine(mem, &eng));
    CHECK(dnnl_engine_get_kind(eng, &eng_kind));
    CHECK(dnnl_memory_get_memory_desc(mem, &md));
    size_t bytes = dnnl_memory_desc_get_size(md);

    if (eng_kind == dnnl_cpu) {
        void *ptr = NULL;
        CHECK(dnnl_memory_get_data_handle(mem, &ptr));
        if (ptr) {
            for (size_t i = 0; i < bytes; ++i) {
                ((char *)handle)[i] = ((char *)ptr)[i];
            }
        }
        else{
            handle = NULL;
        }
    }
#if DNNL_GPU_RUNTIME == DNNL_RUNTIME_OCL
    else if (eng_kind == dnnl_gpu) {
        dnnl_stream_t s;
        cl_command_queue q;
        cl_mem m;

        CHECK(dnnl_memory_get_ocl_mem_object(mem, &m));
        CHECK(dnnl_stream_create(&s, eng, dnnl_stream_default_flags));
        CHECK(dnnl_stream_get_ocl_command_queue(s, &q));

        cl_int ret = clEnqueueReadBuffer(
                q, m, CL_TRUE, 0, bytes, handle, 0, NULL, NULL);
        if (ret != CL_SUCCESS) {
            fprintf(stderr,
                    "clEnqueueReadBuffer failed.\nStatus Code: "
                    "%d\n",
                    ret);
            dnnl_stream_destroy(s);
            exit(1);
        }

        dnnl_stream_destroy(s);
    }
#endif
}

// Read from handle, write to memory
static inline void write_to_dnnl_memory(void *handle, dnnl_memory_t mem) {
    dnnl_engine_t eng;
    dnnl_engine_kind_t eng_kind;
    const dnnl_memory_desc_t *md;

    CHECK(dnnl_memory_get_engine(mem, &eng));
    CHECK(dnnl_engine_get_kind(eng, &eng_kind));
    CHECK(dnnl_memory_get_memory_desc(mem, &md));
    size_t bytes = dnnl_memory_desc_get_size(md);

    if (eng_kind == dnnl_cpu) {
        void *ptr = NULL;
        CHECK(dnnl_memory_get_data_handle(mem, &ptr));
        if (ptr) {
            for (size_t i = 0; i < bytes; ++i) {
                ((char *)handle)[i] = ((char *)ptr)[i];
            }
        }
        else{
            handle = NULL;
        }
    }
#if DNNL_GPU_RUNTIME == DNNL_RUNTIME_OCL
    else if (eng_kind == dnnl_gpu) {
        dnnl_stream_t s;
        cl_command_queue q;
        cl_mem m;

        CHECK(dnnl_memory_get_ocl_mem_object(mem, &m));
        CHECK(dnnl_stream_create(&s, eng, dnnl_stream_default_flags));
        CHECK(dnnl_stream_get_ocl_command_queue(s, &q));

        cl_int ret = clEnqueueWriteBuffer(
                q, m, CL_TRUE, 0, bytes, handle, 0, NULL, NULL);
        if (ret != CL_SUCCESS) {
            fprintf(stderr,
                    "clEnqueueWriteBuffer failed.\nStatus Code: "
                    "%d\n",
                    ret);
            dnnl_stream_destroy(s);
            exit(1);
        }

        dnnl_stream_destroy(s);
    }
#endif
}

#endif
