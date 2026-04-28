// 最小 U55C HLS kernel：ELLPACK 风格的稀疏矩阵向量乘法。
//
// 计算内容：
//   y[row] = scale * sum_k values[row, k] * x[col_idx[row, k]]
//
// 这里故意使用非常简单的稀疏矩阵格式，方便入门：
// - 每一行固定 kSlotsPerRow 个 slot
// - 无效 slot 使用 col_idx = -1, value = 0
// - host 当前生成一个三对角矩阵
//
// 这个文件是 kernel 架构的核心。完整 Alveo 设计还依赖 cfg/u55c.cfg，
// 因为 cfg/u55c.cfg 会把这些 m_axi 端口映射到真实的 U55C HBM bank。

extern "C" {

void krnl_spmv(int num_rows,
               double scale,
               const int* col_idx,
               const double* values,
               const double* x,
               double* y) {
// 标量参数和指针参数都通过 AXI-Lite 控制接口由 host 写入。
#pragma HLS INTERFACE s_axilite port = num_rows bundle = control
#pragma HLS INTERFACE s_axilite port = scale bundle = control
#pragma HLS INTERFACE s_axilite port = col_idx bundle = control
#pragma HLS INTERFACE s_axilite port = values bundle = control
#pragma HLS INTERFACE s_axilite port = x bundle = control
#pragma HLS INTERFACE s_axilite port = y bundle = control
#pragma HLS INTERFACE s_axilite port = return bundle = control

// 每个 m_axi interface 会变成一个 AXI master 端口。
// bundle 名字决定 HLS 侧的端口分组；cfg/u55c.cfg 再把这些端口接到 HBM。
#pragma HLS INTERFACE m_axi port = col_idx offset = slave bundle = gmem_col
#pragma HLS INTERFACE m_axi port = values offset = slave bundle = gmem_val
#pragma HLS INTERFACE m_axi port = x offset = slave bundle = gmem_x
#pragma HLS INTERFACE m_axi port = y offset = slave bundle = gmem_y

    constexpr int kSlotsPerRow = 3;

RowLoop:
    for (int row = 0; row < num_rows; ++row) {
// 请求 HLS 为行循环生成硬件流水线。
// 这和普通软件循环的关键区别是：只要依赖和内存端口允许，不同行的计算
// 可以在硬件中重叠执行。实际 II 要看 HLS 报告，可能受 FP64 运算、
// x[col] 随机读、AXI 调度等因素限制。
#pragma HLS PIPELINE II=1
#pragma HLS LOOP_TRIPCOUNT min=1 max=4096

        double acc = 0.0;

    SlotLoop:
        for (int slot = 0; slot < kSlotsPerRow; ++slot) {
// slot 循环固定只有 3 项，所以这里展开循环，让 HLS 尝试生成 3 路并行
// 乘法/累加路径，而不是保留一个很小的顺序循环。
#pragma HLS UNROLL
            const int idx = row * kSlotsPerRow + slot;
            const int col = col_idx[idx];
            const double val = values[idx];

            if (col >= 0) {
                acc += val * x[col];
            }
        }

        y[row] = scale * acc;
    }
}

}
