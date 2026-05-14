#ifndef PROJECTX_HLS_PIPELINE_DEMO_HPP
#define PROJECTX_HLS_PIPELINE_DEMO_HPP

// 这个头文件同时被 HLS kernel 和 host 引用。
// 这里只放常量和可综合的轻量函数，避免 HLS 前端解析 std::vector
// 这类只属于 host/golden model 的类型。
#define PROJECTX_HLS_PIPELINE_DEMO_MAX_ITEMS 4096
#define PROJECTX_HLS_PIPELINE_DEMO_LANES 4

// compute stage 的核心算子，也就是 FPGA 侧真正要对每个 input[i] 做的计算。
// input[i] = 3*i+1 这件事由 host 生成输入数组完成；这里接收的是已经读到
// FPGA 侧的 value，并把 index 一起纳入计算。
//
// 对每个元素：
//   base = value + index
//   lanes = {base + 0, base + 1, base + 2, base + 3}
//   result = lanes[0] + 2*lanes[1] - lanes[2] + lanes[3]
//
// HLS kernel 里会配合 ARRAY_PARTITION + UNROLL 让 lanes 并行展开；
// host 侧 golden 也调用同一个函数，保证硬件和软件验证使用同一公式。
inline int projectx_hls_pipeline_demo_mix(int value, int index) {
    const int base = value + index;
    int lanes[PROJECTX_HLS_PIPELINE_DEMO_LANES] = {};
    for (int lane = 0; lane < PROJECTX_HLS_PIPELINE_DEMO_LANES; ++lane) {
        lanes[lane] = base + lane;
    }

    return lanes[0] + 2 * lanes[1] - lanes[2] + lanes[3];
}

#endif
