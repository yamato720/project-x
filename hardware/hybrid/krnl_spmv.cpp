// HLS + Chisel/Vivado 混合版本的 U55C kernel。
//
// 这个版本里：
//   1. SpMV 行计算仍由 HLS C++ 完成
//   2. 外积阶段的 FP64 乘法改成 Chisel wrapper + Vivado floating_point IP

#include "krnl_spmv_common.hpp"
#include "outer_product_tile.hpp"

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
#pragma HLS PIPELINE II=1
#pragma HLS LOOP_TRIPCOUNT min=1 max=512
        double acc = 0.0;

    SlotLoop:
        for (int slot = 0; slot < kSlotsPerRow; ++slot) {
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
// ap_ctrl_chain black-box 仍然不能放进 HLS pipeline region，但现在每次调用会
// 产出一个 8x8 tile，显著减少 HLS 逐元素调度开销。
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

            PROJECTX_OUTER_PRODUCT_TILE_CALL(lhs_tile, rhs_tile, tile_out)
            PROJECTX_OUTER_PRODUCT_TILE_COPY_OUTPUTS(tile_out, out_tile)

        OuterProductStoreTile:
            for (int out_idx = 0; out_idx < kOuterTileSize * kOuterTileSize; ++out_idx) {
#pragma HLS PIPELINE II=1
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
