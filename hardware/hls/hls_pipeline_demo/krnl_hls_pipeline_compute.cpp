// kernel 级流水线的 compute kernel。
//
// 这个 kernel 从 source 接收 AXI4-Stream，执行 lane 展开计算和两级寄存器
// 延迟链，再把 output 通过 AXI4-Stream 推给 sink。stage_value 和
// delayed_value 作为观测点直接写回全局内存，便于 host/XS 对照内部行为。

#include "hls_pipeline_stream_common.hpp"

#include <hls_stream.h>

namespace {

int mix_with_unrolled_lanes(int value, int index) {
    // FPGA 侧的核心计算。value 是 source 从设备内存读出的 input[i]，
    // index 是当前 stream 元素的顺序号。
    const int base = value + index;
    int lanes[PROJECTX_HLS_PIPELINE_DEMO_LANES];
#pragma HLS ARRAY_PARTITION variable = lanes complete dim = 1
LaneLoop:
    for (int lane = 0; lane < PROJECTX_HLS_PIPELINE_DEMO_LANES; ++lane) {
#pragma HLS UNROLL
        lanes[lane] = base + lane;
    }
    // stage_value = lanes[0] + 2*lanes[1] - lanes[2] + lanes[3]。
    return lanes[0] + 2 * lanes[1] - lanes[2] + lanes[3];
}

} // namespace

extern "C" {

void krnl_hls_pipeline_compute(hls::stream<ProjectXHlsPipelineWord>& input_stream,
                               hls::stream<ProjectXHlsPipelineWord>& output_stream,
                               int* stage_value,
                               int* delayed_value,
                               int item_count) {
#pragma HLS INTERFACE axis port = input_stream
#pragma HLS INTERFACE axis port = output_stream
#pragma HLS INTERFACE m_axi port = stage_value offset = slave bundle = gmem_stage
#pragma HLS INTERFACE m_axi port = delayed_value offset = slave bundle = gmem_delay
#pragma HLS INTERFACE s_axilite port = stage_value bundle = control
#pragma HLS INTERFACE s_axilite port = delayed_value bundle = control
#pragma HLS INTERFACE s_axilite port = item_count bundle = control
#pragma HLS INTERFACE s_axilite port = return bundle = control

    int delay0 = 0;
    int delay1 = 0;

ComputeLoop:
    for (int index = 0; index < item_count; ++index) {
// 每拍消费一个 stream word，并向 sink 推出一个结果 word。
// 这条循环和 source/sink 的循环位于不同 kernel 中，可以并行运行。
#pragma HLS PIPELINE II = 1
#pragma HLS LOOP_TRIPCOUNT min = 1 max = PROJECTX_HLS_PIPELINE_DEMO_MAX_ITEMS
        const ProjectXHlsPipelineWord input_word = input_stream.read();
        // mixed 是当前元素的 stage_value；delayed 是两级寄存器延迟链
        // 更新前的 delay1，也就是两拍前的 mixed。
        const int mixed =
            mix_with_unrolled_lanes(projectx_hls_pipeline_word_to_int(input_word), index);
        const int delayed = delay1;
        delay1 = delay0;
        delay0 = mixed;

        stage_value[index] = mixed;
        delayed_value[index] = delayed;
        output_stream.write(projectx_hls_pipeline_make_word(mixed + delayed, input_word.last));
    }
}

}
