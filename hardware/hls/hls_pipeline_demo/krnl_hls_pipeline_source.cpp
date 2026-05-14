// kernel 级流水线的 source kernel。
//
// 这个 kernel 从全局内存读入 input，并通过 AXI4-Stream 推给 compute。
// 它是 source -> compute -> sink 三 kernel 流水线的第一段。

#include "hls_pipeline_stream_common.hpp"

#include <hls_stream.h>

extern "C" {

void krnl_hls_pipeline_source(const int* input,
                              hls::stream<ProjectXHlsPipelineWord>& output_stream,
                              int item_count) {
#pragma HLS INTERFACE m_axi port = input offset = slave bundle = gmem_input
#pragma HLS INTERFACE axis port = output_stream
#pragma HLS INTERFACE s_axilite port = input bundle = control
#pragma HLS INTERFACE s_axilite port = item_count bundle = control
#pragma HLS INTERFACE s_axilite port = return bundle = control

SourceLoop:
    for (int index = 0; index < item_count; ++index) {
// 每拍从全局内存读一个元素，并写入跨 kernel stream。
// input[index] 已经由 host 按 input[i] = 3*i+1 生成并同步到设备侧；
// source 只负责把数组元素连续推入 AXI4-Stream。
#pragma HLS PIPELINE II = 1
#pragma HLS LOOP_TRIPCOUNT min = 1 max = PROJECTX_HLS_PIPELINE_DEMO_MAX_ITEMS
        const bool last = (index == item_count - 1);
        output_stream.write(projectx_hls_pipeline_make_word(input[index], last));
    }
}

}
