// kernel 级流水线的 sink kernel。
//
// 这个 kernel 从 compute 接收 AXI4-Stream，并把最终 output 写回全局内存。
// 它是 source -> compute -> sink 三 kernel 流水线的最后一段。

#include "hls_pipeline_stream_common.hpp"

#include <hls_stream.h>

extern "C" {

void krnl_hls_pipeline_sink(hls::stream<ProjectXHlsPipelineWord>& input_stream,
                            int* output,
                            int item_count) {
#pragma HLS INTERFACE axis port = input_stream
#pragma HLS INTERFACE m_axi port = output offset = slave bundle = gmem_output
#pragma HLS INTERFACE s_axilite port = output bundle = control
#pragma HLS INTERFACE s_axilite port = item_count bundle = control
#pragma HLS INTERFACE s_axilite port = return bundle = control

SinkLoop:
    for (int index = 0; index < item_count; ++index) {
// 每拍从跨 kernel stream 取一个结果，并写回全局内存。
// 这里收到的是 compute 已经算好的 output[i] = stage_value[i] + delayed_value[i]。
#pragma HLS PIPELINE II = 1
#pragma HLS LOOP_TRIPCOUNT min = 1 max = PROJECTX_HLS_PIPELINE_DEMO_MAX_ITEMS
        const ProjectXHlsPipelineWord word = input_stream.read();
        output[index] = projectx_hls_pipeline_word_to_int(word);
    }
}

}
