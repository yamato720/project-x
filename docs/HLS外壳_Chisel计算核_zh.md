# HLS 外壳 + Chisel 计算核架构说明

这份文档回答这个问题：

```text
能不能只用 HLS 完成 host 交互接口，
kernel 内部计算全部交给 Chisel，
从而完全掌握流水线？
```

结论先说清楚：

```text
可以做，而且是很实用的混合架构。

但“完全掌握流水线”只对 Chisel 计算核内部成立。
只要全局内存读写仍由 HLS 外壳负责，整体 kernel 的吞吐还会受
HLS 生成的数据搬运逻辑、AXI/HBM 端口、black-box 握手和 Vitis link 结果影响。
```

如果目标是连 AXI read/write engine、outstanding request、burst、bank arbitration
也全部自己掌控，那么更彻底的形态是 **完整 Chisel/RTL kernel**，再用 Vitis RTL
kernel packaging 接入 XRT；那已经不是“只用 HLS 做接口壳”了。

---

## 1. 这个架构长什么样

推荐先想成三层：

```text
host.cpp / XRT
  |
  |  xclbin, kernel args, BO, run.wait()
  v
HLS wrapper kernel
  |
  |  AXI-Lite control
  |  m_axi col_idx / values / x / y / yy_t
  |  stream / scalar / packed-vector interface
  v
Chisel compute core
  |
  |  自己定义流水线、寄存器切分、reduction tree、valid/ready
  v
结果返回 HLS wrapper，再写回 HBM
```

HLS wrapper 负责：

- 生成 XRT 能识别的 kernel 顶层
- 生成 AXI-Lite 控制寄存器
- 接收 host 传进来的 BO base address
- 生成或组织 `m_axi` 访存端口
- 把外部内存数据搬到 Chisel 核可消费的接口
- 把 Chisel 核输出写回 HBM

Chisel compute core 负责：

- 真正的 SpMV / 外积 / reduction / FP pipeline
- 每一级寄存器怎么放
- lane 数量
- valid/ready 背压
- 固定 latency 或可变 latency 协议
- 内部 FIFO、树形归并、token 对齐

---

## 2. 能掌握什么，不能掌握什么

### 可以掌握

只要计算放进 Chisel，你可以明确控制：

- 一行数据进入计算核后经过几个 stage
- slot/lane 是否并行
- FP64 multiply/add 的 wrapper 怎么接
- reduction tree 是线性累加还是树形累加
- 每级之间是否插寄存器
- valid/ready 何时拉高或停顿
- 内部 FIFO 深度和背压策略

也就是说，**Chisel 核内部的流水线结构可以完全由你设计**。

### 不能自动掌握

如果 HLS wrapper 仍然负责访问 HBM，那么下面这些不完全由 Chisel 决定：

- `col_idx[] / values[] / x[]` 读请求如何形成 burst
- 同一个 `m_axi` 端口上多个 load 如何排队
- HLS wrapper 的 load loop / store loop 是否达到目标 II
- Chisel core backpressure 是否反过来拖慢 HLS data mover
- Vitis link 后端对 AXI/HBM 互连的最终排布
- HBM bank 的实际冲突、延迟和 outstanding 能力

所以这个架构能显著提高计算微结构的可控性，但不能让整个 kernel 的端到端时序
脱离 HLS/Vitis 的调度。

---

## 3. 三种可选边界

### 3.1 标量/函数级 black-box

当前 `Project-X` 的外积乘法接入方式属于这一类：

```text
HLS C++ 调用 outer_product_mul_bits(...)
HLS black-box JSON 把这个函数绑定到 Chisel/Vivado RTL
```

优点：

- 改动小
- 容易先跑通
- 适合替换单个算子，例如 FP64 multiply、特殊函数、固定小模块

缺点：

- 如果在 pipeline loop 里频繁调用，容易遇到 HLS black-box 协议限制
- HLS 仍然负责大部分循环和调度
- 计算核粒度太小，不能真正接管整条 SpMV 流水线

适合当前阶段的作用：

```text
验证 HLS <-> Chisel black-box 工具链是否稳定。
```

### 3.2 Streaming Chisel core

这是最推荐的“中间形态”。

HLS wrapper 做数据搬运，把每行或每个 tile 打包成 stream，送入 Chisel：

```text
m_axi col_idx/values/x
  -> HLS read/data-pack stage
  -> AXI-Stream 或 ap_hs stream
  -> Chisel SpMV core
  -> result stream
  -> HLS write stage
  -> m_axi y/yy_t
```

优点：

- Chisel 可以接管完整计算流水线，而不是单个乘法
- HLS 仍然保留 XRT/kernel 接口的便利
- HLS wrapper 可以用 `DATAFLOW` 把 read / compute / write 解耦
- 比完整 RTL kernel 更容易逐步迁移

缺点：

- HLS read/write stage 仍然影响整体吞吐
- stream 宽度、FIFO 深度、backpressure 要认真设计
- black-box 接口协议要稳定，不然 HLS schedule 会很难看

这是 `Project-X` 如果继续演进，比较合理的下一步。

### 3.3 Chisel 自带 AXI master

更激进的做法是让 Chisel core 自己发 AXI read/write 请求：

```text
HLS wrapper
  -> 只传 rows/scale/base_address 等控制参数
Chisel core
  -> 自己拥有 AXI master 读写 HBM
```

这个方向理论上更接近“完全掌握端到端流水线”，但工程复杂度明显上升。

要注意：

- HLS black-box 里暴露复杂 AXI master 接口并不如简单 scalar/stream black-box 稳妥
- Vitis kernel 顶层端口、metadata、connectivity 需要更精确匹配
- 到这个程度时，通常应认真考虑直接做 **Vitis RTL kernel**

也就是说：

```text
如果 Chisel 只做 compute，HLS shell 很合适。
如果 Chisel 连 memory engine 都要做，完整 RTL kernel 往往更干净。
```

---

## 4. 对 Project-X 的推荐方案

当前 `Project-X` 已经有：

- HLS top kernel：`hardware/krnl_spmv.cpp`
- XRT host：`software/host.cpp`
- HBM connectivity：`cfg/u55c.cfg`
- Chisel black-box：`outer_product_mul_bits`

下一步如果想让 kernel 内部计算更多由 Chisel 掌控，建议不要一步到位改完整 RTL kernel，
而是分两步。

### 第一步：把 SpMV row compute 做成 Chisel streaming core

接口可以类似：

```text
row_packet stream in:
  row_id
  slot0_col, slot0_value, slot0_x
  slot1_col, slot1_value, slot1_x
  slot2_col, slot2_value, slot2_x
  scale

row_result stream out:
  row_id
  y_value
```

HLS wrapper 仍然做：

- 读 `col_idx[row * 3 + slot]`
- 读 `values[row * 3 + slot]`
- 按 `col` 读 `x[col]`
- 组包送入 Chisel core
- 接收 `y_value`
- 写 `y[row]` 和 `y_buffer[row]`

Chisel core 做：

- 3 lane slot 乘法
- valid slot mask
- FP64 multiply pipeline
- reduction tree
- scale multiply
- 输出对齐

这样可以把“每行怎么计算”完全交给 Chisel，同时保留 HLS 的 host/HBM 接口便利。

### 第二步：把外积 compute 也做成 Chisel streaming core

当前外积阶段是：

```cpp
for row:
  for col:
    yy_t[row * num_rows + col] = y[row] * y[col]
```

可以改成：

```text
outer_packet stream in:
  row
  col
  y_row
  y_col

outer_result stream out:
  row
  col
  product
```

HLS wrapper 做 y_buffer 读和 yy_t 写，Chisel 做 FP64 multiply pipeline。

这比“每次 C++ 函数调用一个 black-box 乘法”更适合流水化，因为 Chisel core 可以长期保持
streaming 状态，而不是被 HLS loop 调度反复启动/停止。

---

## 5. HLS wrapper 伪代码

下面不是最终代码，只是结构示意：

```cpp
extern "C" void krnl_spmv(... pointers ...) {
#pragma HLS INTERFACE s_axilite ...
#pragma HLS INTERFACE m_axi port=col_idx bundle=gmem_col
#pragma HLS INTERFACE m_axi port=values bundle=gmem_val
#pragma HLS INTERFACE m_axi port=x bundle=gmem_x
#pragma HLS INTERFACE m_axi port=y bundle=gmem_y
#pragma HLS DATAFLOW

    hls::stream<RowPacket> row_in;
    hls::stream<RowResult> row_out;

#pragma HLS STREAM variable=row_in depth=32
#pragma HLS STREAM variable=row_out depth=32

    read_rows(num_rows, scale, col_idx, values, x, row_in);
    chisel_spmv_core(row_in, row_out);
    write_rows(num_rows, row_out, y, y_buffer);
}
```

这里的重点是：

- HLS 的 `read_rows()` 是 data mover
- `chisel_spmv_core()` 是 RTL black-box
- HLS 的 `write_rows()` 是 write-back
- `DATAFLOW` 让三者可以重叠执行

最终性能要分别看：

- `read_rows` 的 II 和 burst 情况
- Chisel core 的内部 II 和 latency
- `write_rows` 的 II
- stream FIFO 是否经常满/空

---

## 6. Chisel core 接口建议

对 streaming core，接口尽量简单：

```text
clock
reset

input valid
input ready
input bits: packed row packet

output valid
output ready
output bits: packed row result
```

建议优先用 ready/valid 或 AXI-Stream 风格，而不是每个字段都做散乱的 `ap_none` 端口。

原因：

- 边界清晰
- 容易插 FIFO
- 方便处理 backpressure
- 和 HLS `hls::stream` / DATAFLOW 的语义更接近
- 后续迁移成完整 RTL kernel 也更自然

---

## 7. “完全掌握流水线”的准确表述

比较准确的说法是：

```text
HLS shell + Chisel compute core：
  完全掌握 compute core 内部流水线；
  部分掌握 wrapper 与 core 之间的流控；
  不能完全掌握 HLS data mover 和 Vitis/HBM 后端行为。

完整 Chisel/RTL kernel：
  可以掌握 compute pipeline + memory engine pipeline；
  但要自己承担 AXI-Lite、AXI master、kernel metadata、仿真和打包复杂度。
```

所以不要把目标写成：

```text
用 HLS 做接口后，整个 kernel 流水线都完全可控。
```

更合理的目标是：

```text
先用 HLS 保留 XRT/HBM 接入效率，
再把 HLS 最难稳定控制的 compute pipeline 下沉到 Chisel。
```

---

## 8. 工程实施清单

如果要在 `Project-X` 里落地，建议按这个顺序做：

1. 定义 `RowPacket` / `RowResult` 的位宽和字段顺序。
2. 用 Chisel 写 `SpmvRowStreamCore`，只处理 stream，不碰 HBM。
3. 给 Chisel core 写 Scala/Verilator 单元测试。
4. 生成 Verilog。
5. 写 HLS black-box JSON，把 `chisel_spmv_core()` 绑定到 RTL。
6. 在 HLS wrapper 中加 `read_rows()` / `write_rows()` 和 `DATAFLOW`。
7. 先跑 `sw_emu`，确认 C++ fallback 和接口行为一致。
8. 跑 HLS csynth，看 wrapper 的 read/write/dataflow II。
9. 跑 Vivado/Vitis link，看最终 timing、HBM port、resource。
10. 再决定是否把 memory engine 也下沉到 Chisel。

---

## 9. 什么时候该升级到完整 RTL kernel

出现下面情况时，HLS shell 的收益会下降：

- HLS data mover 无法形成你想要的 burst/outstanding 结构
- 需要自己调度多个 HBM bank 的读写仲裁
- 需要精确控制 AXI ID、request queue、write response
- 需要多级 tile buffer / prefetch / reorder buffer
- HLS wrapper 的 II 成为主要瓶颈
- black-box 与 HLS pipeline 的协议限制频繁卡住设计

这时更合理的是：

```text
Chisel 写完整 kernel RTL
Vitis 用 RTL kernel packaging 接入 XRT
host.cpp 基本仍可沿用 XRT 调用方式
```

host 侧看到的仍然可以是一个普通 xclbin kernel；变化主要在 kernel 硬件实现和打包方式。

---

## 10. 对当前 Project-X 的结论

当前阶段最推荐的方向是：

```text
保留 HLS top-level wrapper。
把 SpMV row compute 从 HLS loop 逐步下沉到 Chisel streaming core。
外积阶段也改成 streaming core，而不是在 loop 里反复调用单次 multiply black-box。
```

这样能获得：

- host/XRT 接口继续简单
- HBM bank 配置继续走 `cfg/u55c.cfg`
- Chisel 接管核心计算流水线
- 比完整 RTL kernel 更容易调试和回退

但需要接受：

```text
端到端性能仍必须看 HLS wrapper + Chisel core + Vitis link 的整体报告。
不能只看 Chisel core 内部 II。
```

