// 单 XRT kernel 内部的 HLS 流水线覆盖 demo。
//
// top 函数里的 DATAFLOW 区域用于表达 kernel 内部的任务级流水：
// load、compute、store 是三个独立 stage，中间通过 hls::stream FIFO 连接。
// 每个 stage 自己又有 II=1 的循环流水，因此这个 kernel 同时覆盖：
//   1. stage 之间的 task/dataflow pipeline
//   2. stage 内部的 loop pipeline
//   3. compute 内部的展开、数组分割和寄存器延迟链
//
// 注意：这个文件只有一个 XRT 顶层 kernel。真正多个 kernel 之间的
// source -> compute -> sink 流水线在 krnl_hls_pipeline_stream.cpp 里。

#include "hls_pipeline_demo.hpp"

#include <hls_stream.h>

namespace {

// load stage 送给 compute stage 的流元素。
// index 跟随数据一起传递，store stage 写回时不依赖隐含顺序。
struct LoadPacket {
    int index;
    int value;
};

// compute stage 送给 store stage 的流元素。
// 这里故意保留 stage_value 和 delayed_value 两个观测点，
// host 可以同时检查“当前计算值”和“寄存器链延迟值”。
struct ComputePacket {
    int index;
    int stage_value;
    int delayed_value;
    int output_value;
};

void load_stage(int item_count, const int* input, hls::stream<LoadPacket>& load_to_compute) {
    // scratch 是一个人为放进 demo 的本地 BRAM，用来覆盖 BIND_STORAGE
    // 这种高级存储绑定形式。真实算法不一定需要这层缓存。
    int scratch[PROJECTX_HLS_PIPELINE_DEMO_MAX_ITEMS];
#pragma HLS BIND_STORAGE variable = scratch type = ram_2p impl = bram

LoadLoop:
    for (int index = 0; index < item_count; ++index) {
// 每拍读一个输入元素并写入下游 FIFO。
// LOOP_TRIPCOUNT 只给 HLS 报告/估算用，不改变运行时边界。
#pragma HLS PIPELINE II = 1
#pragma HLS LOOP_TRIPCOUNT min = 1 max = PROJECTX_HLS_PIPELINE_DEMO_MAX_ITEMS
        const int value = input[index];
        scratch[index] = value;
        load_to_compute.write(LoadPacket{index, scratch[index]});
    }
}

void compute_stage(int item_count,
                   hls::stream<LoadPacket>& load_to_compute,
                   hls::stream<ComputePacket>& compute_to_store) {
    // 两级寄存器延迟链。它让 output 依赖更早周期的 stage_value，
    // 方便后续用波形或 XS 等价体观察“流水线内部状态”和“最终输出”的关系。
    int delay0 = 0;
    int delay1 = 0;

ComputeLoop:
    for (int index = 0; index < item_count; ++index) {
// compute stage 每拍消费一个 load packet，并产生一个 compute packet。
#pragma HLS PIPELINE II = 1
#pragma HLS LOOP_TRIPCOUNT min = 1 max = PROJECTX_HLS_PIPELINE_DEMO_MAX_ITEMS
        const LoadPacket packet = load_to_compute.read();
        // FPGA 侧的实际计算从这里开始。packet.value 已经是 host 生成并
        // 写入设备内存的 input[i]，kernel 只是连续读出来使用。
        const int base = packet.value + packet.index;

        // lanes 是局部并行计算的载体。ARRAY_PARTITION complete 让数组元素
        // 变成独立寄存器，再配合下面的 UNROLL 表达 lane 级并行。
        int lanes[PROJECTX_HLS_PIPELINE_DEMO_LANES];
#pragma HLS ARRAY_PARTITION variable = lanes complete dim = 1
    LaneLoop:
        for (int lane = 0; lane < PROJECTX_HLS_PIPELINE_DEMO_LANES; ++lane) {
#pragma HLS UNROLL
            lanes[lane] = base + lane;
        }

        // stage_value 是当前元素在 compute stage 的组合/展开计算结果：
        // lanes[0] + 2*lanes[1] - lanes[2] + lanes[3]。
        const int mixed = lanes[0] + 2 * lanes[1] - lanes[2] + lanes[3];
        // delayed 取的是更新前的 delay1，所以它代表两拍前进入
        // compute stage 的 mixed 值。前两个元素会看到初始 0。
        const int delayed = delay1;
        delay1 = delay0;
        delay0 = mixed;

        compute_to_store.write(ComputePacket{packet.index, mixed, delayed, mixed + delayed});
    }
}

void store_stage(int item_count,
                 hls::stream<ComputePacket>& compute_to_store,
                 int* output,
                 int* stage_value,
                 int* delayed_value) {
StoreLoop:
    for (int index = 0; index < item_count; ++index) {
// store stage 每拍写回一个结果，同时把中间观测点也写到独立数组。
// 这样 host 不只检查最终 output，也能检查流水线内部暴露出来的值。
#pragma HLS PIPELINE II = 1
#pragma HLS LOOP_TRIPCOUNT min = 1 max = PROJECTX_HLS_PIPELINE_DEMO_MAX_ITEMS
        const ComputePacket packet = compute_to_store.read();
        output[packet.index] = packet.output_value;
        stage_value[packet.index] = packet.stage_value;
        delayed_value[packet.index] = packet.delayed_value;
    }
}

} // namespace

extern "C" {

void krnl_hls_pipeline_demo(int item_count,
                            const int* input,
                            int* output,
                            int* stage_value,
                            int* delayed_value) {
// s_axilite 定义 host 可配置的控制寄存器接口。
// XRT host 看到的参数顺序就是函数签名顺序：
//   arg0 item_count, arg1 input, arg2 output, arg3 stage_value, arg4 delayed_value
#pragma HLS INTERFACE s_axilite port = item_count bundle = control
#pragma HLS INTERFACE s_axilite port = input bundle = control
#pragma HLS INTERFACE s_axilite port = output bundle = control
#pragma HLS INTERFACE s_axilite port = stage_value bundle = control
#pragma HLS INTERFACE s_axilite port = delayed_value bundle = control
#pragma HLS INTERFACE s_axilite port = return bundle = control

// 每个数组端口单独放在一个 m_axi bundle，方便在报告里观察端口，
// 也方便以后按端口名做 HBM/connectivity 绑定。
#pragma HLS INTERFACE m_axi port = input offset = slave bundle = gmem_input
#pragma HLS INTERFACE m_axi port = output offset = slave bundle = gmem_output
#pragma HLS INTERFACE m_axi port = stage_value offset = slave bundle = gmem_stage
#pragma HLS INTERFACE m_axi port = delayed_value offset = slave bundle = gmem_delay

    // 两条 stream 是三个 stage 之间的硬件 FIFO。
    // depth=32 给 dataflow stage 一定解耦空间，也让 sw_emu 输出能看到 FIFO 深度统计。
    hls::stream<LoadPacket> load_to_compute("load_to_compute");
    hls::stream<ComputePacket> compute_to_store("compute_to_store");
#pragma HLS STREAM variable = load_to_compute depth = 32
#pragma HLS STREAM variable = compute_to_store depth = 32

// DATAFLOW 是这个 demo 的 kernel 级流水核心：
// 三个函数会被 HLS 尝试并行调度，而不是顺序执行完整 load 再 compute 再 store。
#pragma HLS DATAFLOW
    load_stage(item_count, input, load_to_compute);
    compute_stage(item_count, load_to_compute, compute_to_store);
    store_stage(item_count, compute_to_store, output, stage_value, delayed_value);
}

}
