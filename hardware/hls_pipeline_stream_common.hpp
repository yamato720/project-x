#ifndef PROJECTX_HLS_PIPELINE_STREAM_COMMON_HPP
#define PROJECTX_HLS_PIPELINE_STREAM_COMMON_HPP

#include "hls_pipeline_demo.hpp"

#include <ap_axi_sdata.h>

// 三 kernel 版本共用的 AXI4-Stream word。
// 32 bit data 承载一个 int；keep/strb/last 用标准 AXIS side-channel。
using ProjectXHlsPipelineWord = ap_axiu<32, 0, 0, 0>;

inline ProjectXHlsPipelineWord projectx_hls_pipeline_make_word(int value, bool last) {
    ProjectXHlsPipelineWord word;
    word.data = value;
    word.keep = -1;
    word.strb = -1;
    word.last = last ? 1 : 0;
    return word;
}

inline int projectx_hls_pipeline_word_to_int(const ProjectXHlsPipelineWord& word) {
    return static_cast<int>(word.data);
}

#endif
