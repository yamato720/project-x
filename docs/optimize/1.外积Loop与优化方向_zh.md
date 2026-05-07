# Project-X：外积 Loop 与优化方向

这份文档专门回答两个问题：

1. 当前 `Project-X` 里的“外积 loop”到底指哪一段？
2. 目前 HLS 和 Chisel 版本分别是怎么做这段的？后续如果要继续优化，推荐怎么改？

## 1. 外积 loop 是什么

`Project-X` 在算完：

```text
y = scale * A * x
```

之后，还会继续生成：

```text
yy_t = y * y^T
```

这里的 `yy_t` 是一个完整外积矩阵，不是只算上三角。

所以对 `rows = 512` 来说，外积阶段要生成：

```text
512 x 512 = 262144
```

个结果元素。

对应到代码里，就是这段双层循环。

纯 HLS 版：

- [hls/krnl_spmv.cpp](/home/pyx/ProjectFS/Project-X/hardware/hls/krnl_spmv.cpp:65)

混合版：

- [hybrid/krnl_spmv.cpp](/home/pyx/ProjectFS/Project-X/hardware/hybrid/krnl_spmv.cpp:78)

`chisel_core` 版：

- [chisel_core/krnl_spmv.cpp](/home/pyx/ProjectFS/Project-X/hardware/chisel_core/krnl_spmv.cpp:120)

逻辑上都一样：

```cpp
for (int row = 0; row < num_rows; ++row) {
    const double lhs = y_buffer[row];
    for (int col = 0; col < num_rows; ++col) {
        yy_t[row * num_rows + col] = lhs * y_buffer[col];
    }
}
```

## 2. 当前 HLS 版本怎么做

纯 HLS 版在内层外积 loop 上是这样写的：

- [hls/krnl_spmv.cpp](/home/pyx/ProjectFS/Project-X/hardware/hls/krnl_spmv.cpp:71)

关键点：

```cpp
#pragma HLS PIPELINE II=1
yy_t[row * num_rows + col] = lhs * y_buffer[col];
```

也就是说：

- HLS 自己生成外积乘法 datapath
- 内层 `OuterProductColLoop` 允许尝试流水化
- 当前报告里，这也是为什么纯 HLS 版本明显更快

## 3. 当前 Chisel 版本怎么做

`hybrid` 和 `chisel_core` 都把外积乘法改成了 black-box 调用。

`hybrid`：

- [hybrid/krnl_spmv.cpp](/home/pyx/ProjectFS/Project-X/hardware/hybrid/krnl_spmv.cpp:84)

`chisel_core`：

- [chisel_core/krnl_spmv.cpp](/home/pyx/ProjectFS/Project-X/hardware/chisel_core/krnl_spmv.cpp:126)

关键点都是：

```cpp
#pragma HLS PIPELINE off
outer_product_mul_bits(...);
```

这里必须 `PIPELINE off` 的原因是：

- `outer_product_mul_bits` 当前是 `ap_ctrl_chain` black-box
- Vitis HLS 不允许这类 black-box 直接处在 pipeline region 里

所以虽然 black-box 本身声明的是：

```text
latency = 6 cycles
II = 1
```

但 HLS 外层 loop 没有把整个外积阶段铺成连续流水。

## 4. 为什么它成了当前瓶颈

从工作量上看：

- 行内 `SpMV`
  `rows * 3`
- 外积
  `rows * rows`

当 `rows = 512` 时：

```text
SpMV slot 数   = 512 * 3      = 1536
外积元素个数   = 512 * 512    = 262144
```

外积比行内 `SpMV` 大约多：

```text
262144 / 1536 ≈ 171 倍
```

所以只要外积阶段没有铺满流水，总时间自然会被它主导。

这也是为什么你看到：

```text
hls          约 11 ms
hybrid       约 78 ms
chisel_core  约 79 ms
```

`hybrid` 和 `chisel_core` 差不多，不是因为 Chisel 优化没意义，而是因为它们共同的主瓶颈都还是同一段外积。

## 5. 报告里怎么体现这个瓶颈

当前 `chisel_core` 的 HLS 报告里可以直接看到：

- [reports/chisel_core/hw/xilinx_u55c_gen3x16_xdma_3_202210_1/krnl_spmv/hls_reports/krnl_spmv_csynth.rpt](/home/pyx/ProjectFS/Project-X/reports/chisel_core/hw/xilinx_u55c_gen3x16_xdma_3_202210_1/krnl_spmv/hls_reports/krnl_spmv_csynth.rpt)

关键信息：

```text
spmv_row_muladd_bits   latency = 24 cycles, II = 1
outer_product_mul_bits latency =  6 cycles, II = 1
OuterProductColLoop    latency = 78 cycles, Pipelined = no
```

这说明：

- 单个乘法 black-box 自己不慢
- 慢的是“外层每个元素一次 black-box 调用”的组织方式

## 6. 当前 HLS 和 Chisel 的边界

当前边界是这样：

### HLS

- 管 AXI-Lite 控制
- 管 HBM `m_axi`
- 管 `y_buffer`
- 管外层 loop 调度

### Chisel

- `outer_product_mul_bits` 只负责单次 FP64 乘法
- `spmv_row_muladd_bits` 只负责单行 FP64 乘加

这就意味着：

```text
Chisel 只接管了“算子”
没有接管“外积整段流水”
```

## 7. 推荐优化方向

如果目标是让 `chisel_core` 真正接近甚至超过纯 HLS 版本，最值得改的不是继续抠单个乘法 IP，而是把外积从：

```text
元素级 black-box 调用
```

改成：

```text
tile 级 black-box 调用
```

## 8. 推荐的 tile 方案

### 8.1 第一版：8x8 tile

建议新增一个更大粒度的 black-box，例如：

```text
outer_product_tile_bits
```

输入：

```text
lhs_vec[8]
rhs_vec[8]
```

输出：

```text
out_mat[64]
```

内部计算：

```text
out[i][j] = lhs_vec[i] * rhs_vec[j]
```

### 8.2 HLS 外壳怎么改

当前是：

```cpp
for row:
  for col:
    yy_t[row * rows + col] = ...
```

改成：

```cpp
for (row_base = 0; row_base < rows; row_base += 8) {
    load lhs_tile[8];
    for (col_base = 0; col_base < rows; col_base += 8) {
        load rhs_tile[8];
        outer_product_tile_bits(lhs_tile, rhs_tile, out_tile);
        write out_tile[64] to yy_t;
    }
}
```

### 8.3 为什么这个方向更合理

对于 `rows = 512`：

元素级调用次数是：

```text
512 * 512 = 262144
```

如果改成 `8x8` tile：

```text
(512 / 8) * (512 / 8) = 4096
```

也就是：

```text
black-box 调用次数减少 64 倍
```

这不代表总时间必然减少 64 倍，但它把瓶颈从：

```text
HLS 每个元素调一次 black-box 的调度开销
```

转成：

```text
真正的 tile 内乘法流水 + 写回带宽
```

这才是更适合 Chisel 发力的位置。

## 9. 建议的实现顺序

建议按下面顺序做：

1. 保持 `spmv_row_muladd_bits` 不动
2. 新增 `outer_product_tile_bits`
3. 先做 `8x8` tile
4. HLS 里把外积改成 tile 双层循环
5. 先跑 `sw_emu`
6. 再跑 `hw`
7. 观察：
   - `kernel_min / kernel_avg / kernel_max`
   - HLS report
   - DSP / BRAM 占用
8. 再决定是否上更大的 tile 或更高并行度

## 10. 一句话总结

当前“外积 loop”就是生成 `yy_t = y * y^T` 的双层循环。  
纯 HLS 版让 HLS 自己把它流水化；当前 Chisel 版只替掉了其中单个乘法算子，但 HLS 仍然按元素级调用 black-box，所以总时间主要卡在这一段。

如果要继续优化，最推荐的方向不是继续抠单次 `outer_product_mul_bits`，而是把外积改成：

```text
tile 级 Chisel black-box
```

让 Chisel 接管一整块外积结果的流水输出。这样才真正有机会把当前的 7 倍差距追回来。
