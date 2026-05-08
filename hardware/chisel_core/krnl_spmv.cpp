// HLS 外壳 + Chisel 计算核版本的 U55C kernel。
//
// 这个版本里 HLS 仍然负责：
//   1. XRT 可见的顶层函数
//   2. AXI-Lite 控制寄存器
//   3. HBM m_axi 端口与数据搬运
//
// Chisel/Vivado black-box 负责：
//   1. 单行 SpMV 的 FP64 乘加
//   2. 外积阶段的 FP64 乘法

#include "krnl_spmv_common.hpp"
#include "outer_product_tile.hpp"

// 单行 SpMV FP64 乘加 black-box。
void spmv_row_muladd_bits(unsigned long long scale_bits,
                          unsigned long long val0_bits,
                          unsigned long long x0_bits,
                          unsigned long long val1_bits,
                          unsigned long long x1_bits,
                          unsigned long long val2_bits,
                          unsigned long long x2_bits,
                          unsigned long long& result_bits);

#ifndef __SYNTHESIS__
void spmv_row_muladd_bits(unsigned long long scale_bits,
                          unsigned long long val0_bits,
                          unsigned long long x0_bits,
                          unsigned long long val1_bits,
                          unsigned long long x1_bits,
                          unsigned long long val2_bits,
                          unsigned long long x2_bits,
                          unsigned long long& result_bits) {
    const double scale = projectx_bits_to_double(scale_bits);
    const double acc = projectx_bits_to_double(val0_bits) * projectx_bits_to_double(x0_bits) +
                       projectx_bits_to_double(val1_bits) * projectx_bits_to_double(x1_bits) +
                       projectx_bits_to_double(val2_bits) * projectx_bits_to_double(x2_bits);
    result_bits = projectx_double_to_bits(scale * acc);
}
#endif

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
    constexpr int kOuterTileSize = PROJECTX_OUTER_TILE_SIZE;
    double y_buffer[kMaxRows];
#pragma HLS BIND_STORAGE variable = y_buffer type = ram_2p impl = bram

RowLoop:
    for (int row = 0; row < num_rows; ++row) {
// 这里会调用 ap_ctrl_chain 类型的 RTL black-box，所以 RowLoop 本身不能再放进
// HLS pipeline region。行内真正的 FP64 流水线由 spmv_row_muladd_bits 内部接管。
#pragma HLS PIPELINE off
#pragma HLS LOOP_TRIPCOUNT min=1 max=512
        unsigned long long row_value_bits = 0;
        unsigned long long val_bits[kSlotsPerRow];
        unsigned long long x_bits[kSlotsPerRow];
#pragma HLS ARRAY_PARTITION variable = val_bits complete
#pragma HLS ARRAY_PARTITION variable = x_bits complete

    SlotLoadLoop:
        for (int slot = 0; slot < kSlotsPerRow; ++slot) {
// 这里仍然完全展开 3 个 slot 的读入和打包，让 HLS 外壳尽量少做顺序控制。
#pragma HLS UNROLL
            const int idx = row * kSlotsPerRow + slot;
            const int col = col_idx[idx];
            const double val = values[idx];
            const double x_value = (col >= 0) ? x[col] : 0.0;
            val_bits[slot] = projectx_double_to_bits((col >= 0) ? val : 0.0);
            x_bits[slot] = projectx_double_to_bits(x_value);
        }

        spmv_row_muladd_bits(projectx_double_to_bits(scale),
                             val_bits[0],
                             x_bits[0],
                             val_bits[1],
                             x_bits[1],
                             val_bits[2],
                             x_bits[2],
                             row_value_bits);
        const double row_value = projectx_bits_to_double(row_value_bits);
        y[row] = row_value;
        y_buffer[row] = row_value;
    }

OuterProductRowLoop:
    for (int row = 0; row < num_rows; row += kOuterTileSize) {
#pragma HLS LOOP_FLATTEN off
#pragma HLS LOOP_TRIPCOUNT min=1 max=64
        unsigned long long lhs_tile[kOuterTileSize];
#pragma HLS ARRAY_PARTITION variable = lhs_tile complete

    OuterProductLoadLhsTile:
        for (int tile_row = 0; tile_row < kOuterTileSize; ++tile_row) {
#pragma HLS UNROLL
            const int global_row = row + tile_row;
            const double lhs = (global_row < num_rows) ? y_buffer[global_row] : 0.0;
            lhs_tile[tile_row] = projectx_double_to_bits(lhs);
        }

    OuterProductColTileLoop:
        for (int col = 0; col < num_rows; col += kOuterTileSize) {
// 外积阶段仍然调用 ap_ctrl_chain black-box，但粒度提升到 8x8 tile。
#pragma HLS PIPELINE off
#pragma HLS LOOP_TRIPCOUNT min=1 max=64
            unsigned long long rhs_tile[kOuterTileSize];
            unsigned long long out_tile[kOuterTileSize * kOuterTileSize];
#pragma HLS ARRAY_PARTITION variable = rhs_tile complete
#pragma HLS ARRAY_PARTITION variable = out_tile complete
            PROJECTX_OUTER_PRODUCT_TILE_DECLARE_OUTPUTS(tile_out)

        OuterProductLoadRhsTile:
            for (int tile_col = 0; tile_col < kOuterTileSize; ++tile_col) {
#pragma HLS UNROLL
                const int global_col = col + tile_col;
                const double rhs = (global_col < num_rows) ? y_buffer[global_col] : 0.0;
                rhs_tile[tile_col] = projectx_double_to_bits(rhs);
            }

            // 这三个宏都定义在 generated/outer/outer_product_tile.hpp 里：
            // 1. DECLARE_OUTPUTS: 声明 64 个标量输出临时变量，对应 out0_bits..out63_bits
            // 2. CALL: 把 lhs_tile[8] / rhs_tile[8] 展开成一次 HLS 调用
            //    outer_product_tile_bits(lhs0_bits..lhs7_bits,
            //                            rhs0_bits..rhs7_bits,
            //                            out0_bits..out63_bits)
            // 3. COPY_OUTPUTS: 再把 64 个标量结果拷回 out_tile[]
            //
            // v++/Vitis HLS 会通过 add_outer_product_blackbox.tcl 加入的
            // outer_product_tile.json，把这个 C 函数名绑定到 Chisel RTL 顶层
            // outer_product_tile_bits；RTL 内部再并行调 64 颗 FP64 multiply IP。
            PROJECTX_OUTER_PRODUCT_TILE_CALL(lhs_tile, rhs_tile, tile_out)
            PROJECTX_OUTER_PRODUCT_TILE_COPY_OUTPUTS(tile_out, out_tile)

        OuterProductStoreTile:
            for (int out_idx = 0; out_idx < kOuterTileSize * kOuterTileSize; ++out_idx) {
#pragma HLS PIPELINE II=1
                // out_tile[] 按 row-major 存放：out_idx = tile_row * 8 + tile_col。
                const int tile_row = out_idx / kOuterTileSize;
                const int tile_col = out_idx % kOuterTileSize;
                const int global_row = row + tile_row;
                const int global_col = col + tile_col;
                if (global_row < num_rows && global_col < num_rows) {
                    yy_t[global_row * num_rows + global_col] =
                        projectx_bits_to_double(out_tile[out_idx]);
                }
            }
        }
    }
}

}
