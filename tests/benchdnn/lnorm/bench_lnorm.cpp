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

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sstream>

#include "dnnl.h"

#include "dnnl_common.hpp"
#include "dnnl_debug.hpp"
#include "dnnl_memory.hpp"
#include "parser.hpp"

#include "lnorm/lnorm.hpp"

namespace lnorm {

dims_t dims;
std::vector<dir_t> dir {FWD_D};
std::vector<dnnl_data_type_t> dt {dnnl_f32};
std::vector<dnnl_format_tag_t> data_tag {dnnl_tnc};
std::vector<dnnl_format_tag_t> stat_tag {dnnl_tn};
std::vector<flags_t> flags {0};
std::vector<bool> inplace {true};

check_alg_t check_alg = ALG_AUTO;
attr_t attr;
const char *pattern = NULL;
const char *skip_impl = "";
bool allow_unimpl = false;
const char *perf_template_csv
        = "perf,%engine%,%dir%,%dt%,%tag%,%stat_tag%,%flags%,%DESC%,"
          "%-time%,%0time%";
const char *perf_template_def
        = "perf,%engine%,%desc%,%-time%,%-Gbw%,%0time%,%0Gbw%";
const char *perf_template = perf_template_def;

void reset_parameters() {
    dir = {FWD_D};
    dt = {dnnl_f32};
    data_tag = {dnnl_tnc};
    stat_tag = {dnnl_tn};
    flags = {0};
    inplace = {true};
    attr = attr_t();
    pattern = NULL;
    skip_impl = "";
    allow_unimpl = false;
}

void check_correctness() {
    for_(const auto &i_dir : dir)
    for_(const auto &i_dt : dt)
    for_(const auto &i_data_tag : data_tag)
    for_(const auto &i_stat_tag : stat_tag)
    for_(const auto &i_flags : flags)
    for (const auto &i_inplace : inplace) {
        const prb_t p(dims, i_data_tag, i_stat_tag, i_dir, i_dt, i_flags,
                i_inplace, attr, check_alg);
        std::stringstream ss;
        ss << p;
        const std::string cpp_pstr = ss.str();
        const char *pstr = cpp_pstr.c_str();

        if (pattern && !match_regex(pstr, pattern)) return;
        print(1, "run: %s\n", pstr);

        res_t res {};
        const int status = doit(&p, &res);

        bool want_perf_report = false;
        parse_result(res, want_perf_report, allow_unimpl, status, pstr);

        if (want_perf_report && bench_mode & PERF) {
            perf_report_t pr(perf_template);
            pr.report(&p, &res, pstr);
        }

        benchdnn_stat.tests++;
    }
}

int bench(int argc, char **argv) {
    driver_name = "lnorm";
    using namespace parser;
    for (; argc > 0; --argc, ++argv) {
        const bool parsed_options = false || parse_bench_settings(argv[0])
                || parse_batch(bench, argv[0]) || parse_dir(dir, argv[0])
                || parse_dt(dt, argv[0])
                || parse_tag(data_tag, argv[0], "data_tag")
                || parse_tag(stat_tag, argv[0], "stat_tag")
                || parse_vector_option(flags, str2flags, argv[0], "flags")
                || parse_inplace(inplace, argv[0]) || parse_attr(attr, argv[0])
                || parse_test_pattern_match(pattern, argv[0])
                || parse_skip_impl(skip_impl, argv[0])
                || parse_allow_unimpl(allow_unimpl, argv[0])
                || parse_perf_template(perf_template, perf_template_def,
                        perf_template_csv, argv[0])
                || parse_reset(reset_parameters, argv[0]);
        if (!parsed_options) {
            catch_unknown_options(argv[0]);

            parse_dims(dims, argv[0]);
            check_correctness();
        }
    }

    return parse_last_argument();
}

} // namespace lnorm
