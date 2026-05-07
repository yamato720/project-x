// 纯 HLS 版本的 U55C kernel。
//
// 这个版本里两段计算都由 HLS C++ 自己综合：
//   1. SpMV 行计算
//   2. y * y^T 外积
//
// 三种 variant 的 host/XRT 接口保持一致，区别只在 kernel 内部的计算实现。

#include "krnl_spmv_common.hpp"

extern "C" {

void krnl_spmv(int num_rows,
               double scale,
               const int* col_idx,
               const double* values,
               const double* x,
               double* y,
               double* yy_t) {
#pragma HLS INTERFACE s_axilite port = num_rows bundle = control
#pragma HLS INTERFACE s_axilite port = scale bundle = control
#pragma HLS INTERFACE s_axilite port = col_idx bundle = control
#pragma HLS INTERFACE s_axilite port = values bundle = control
#pragma HLS INTERFACE s_axilite port = x bundle = control
#pragma HLS INTERFACE s_axilite port = y bundle = control
#pragma HLS INTERFACE s_axilite port = yy_t bundle = control
#pragma HLS INTERFACE s_axilite port = return bundle = control

#pragma HLS INTERFACE m_axi port = col_idx offset = slave bundle = gmem_col
#pragma HLS INTERFACE m_axi port = values offset = slave bundle = gmem_val
#pragma HLS INTERFACE m_axi port = x offset = slave bundle = gmem_x
#pragma HLS INTERFACE m_axi port = y offset = slave bundle = gmem_y
#pragma HLS INTERFACE m_axi port = yy_t offset = slave bundle = gmem_yyt

    constexpr int kSlotsPerRow = PROJECTX_SLOTS_PER_ROW;
    constexpr int kMaxRows = PROJECTX_MAX_ROWS;
    double y_buffer[kMaxRows];
#pragma HLS BIND_STORAGE variable = y_buffer type = ram_2p impl = bram

RowLoop:
    for (int row = 0; row < num_rows; ++row) {
#pragma HLS PIPELINE II=1
#pragma HLS LOOP_TRIPCOUNT min=1 max=512
        double acc = 0.0;

    SlotLoop:
        for (int slot = 0; slot < kSlotsPerRow; ++slot) {
// 当前 ELLPACK demo 固定每行 3 个 slot，所以这里直接完全展开。
// 这里的 3 不是 HLS 自动推出来的，而是 host 与 kernel 约定的数据布局。
#pragma HLS UNROLL
            const int idx = row * kSlotsPerRow + slot;
            const int col = col_idx[idx];
            const double val = values[idx];

            if (col >= 0) {
                acc += val * x[col];
            }
        }

        const double row_value = scale * acc;
        y[row] = row_value;
        y_buffer[row] = row_value;
    }

OuterProductRowLoop:
    for (int row = 0; row < num_rows; ++row) {
#pragma HLS LOOP_FLATTEN off
#pragma HLS LOOP_TRIPCOUNT min=1 max=512
        const double lhs = y_buffer[row];

    OuterProductColLoop:
        for (int col = 0; col < num_rows; ++col) {
// 纯 HLS 版本没有 RTL black-box 协议限制，允许 HLS 尝试把外积内层
// 循环做成流水。
#pragma HLS PIPELINE II=1
#pragma HLS LOOP_TRIPCOUNT min=1 max=512
            yy_t[row * num_rows + col] = lhs * y_buffer[col];
        }
    }
}

}
