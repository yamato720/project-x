// 顶层函数 PIPELINE 覆盖 demo。
//
// krnl_hls_pipeline_demo 展示的是 top DATAFLOW；source/compute/sink 展示的是
// loop 级 II=1 pipeline。这个 kernel 单独把 PIPELINE 放在 XRT kernel
// 顶层函数本身，用来让 HLS schedule 的 top-level `* Pipeline` 计数变为 1。

#include "hls_pipeline_demo.hpp"

namespace {

int top_pipeline_mix(int value, int index) {
#pragma HLS INLINE
    const int base = value + index;
    int lanes[PROJECTX_HLS_PIPELINE_DEMO_LANES];
#pragma HLS ARRAY_PARTITION variable = lanes complete dim = 1
TopPipelineLaneLoop:
    for (int lane = 0; lane < PROJECTX_HLS_PIPELINE_DEMO_LANES; ++lane) {
#pragma HLS UNROLL
        lanes[lane] = base + lane;
    }
    return lanes[0] + 2 * lanes[1] - lanes[2] + lanes[3];
}

} // namespace

extern "C" {

void krnl_hls_top_pipeline_demo(const int* input,
                                int* output,
                                int* stage_value,
                                int* delayed_value) {
#pragma HLS INTERFACE m_axi port = input offset = slave bundle = gmem_input
#pragma HLS INTERFACE m_axi port = output offset = slave bundle = gmem_output
#pragma HLS INTERFACE m_axi port = stage_value offset = slave bundle = gmem_stage
#pragma HLS INTERFACE m_axi port = delayed_value offset = slave bundle = gmem_delay
#pragma HLS INTERFACE s_axilite port = input bundle = control
#pragma HLS INTERFACE s_axilite port = output bundle = control
#pragma HLS INTERFACE s_axilite port = stage_value bundle = control
#pragma HLS INTERFACE s_axilite port = delayed_value bundle = control
#pragma HLS INTERFACE s_axilite port = return bundle = control

// 函数级 PIPELINE 是这个 kernel 的重点。下面的两个小循环完全展开，
// 避免 HLS 把核心流水线落在子 loop 报告里，而是落在 top schedule 上。
#pragma HLS PIPELINE II = 1
    int mixed[PROJECTX_HLS_TOP_PIPELINE_DEMO_ITEMS];
#pragma HLS ARRAY_PARTITION variable = mixed complete dim = 1

TopPipelineReadCompute:
    for (int index = 0; index < PROJECTX_HLS_TOP_PIPELINE_DEMO_ITEMS; ++index) {
#pragma HLS UNROLL
        mixed[index] = top_pipeline_mix(input[index], index);
    }

TopPipelineStore:
    for (int index = 0; index < PROJECTX_HLS_TOP_PIPELINE_DEMO_ITEMS; ++index) {
#pragma HLS UNROLL
        const int delayed = (index >= 2) ? mixed[index - 2] : 0;
        stage_value[index] = mixed[index];
        delayed_value[index] = delayed;
        output[index] = mixed[index] + delayed;
    }
}

}
