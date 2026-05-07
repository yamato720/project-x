# Project-X：HLS pragma 到 U55C 硬件对照

这份文档是给 `hardware/krnl_spmv.cpp` 配的“速查版”。

目标不是再讲一遍完整原理，而是把你在代码里看到的每条 `#pragma HLS`
快速对到：

1. HLS 会生成什么接口/结构
2. 在当前 `Project-X` 里它和哪段 host/cfg 联动
3. 放到 U55C 上大致对应什么硬件路径
4. 你以后该去哪里看验证证据

相关文件：

```text
~/ProjectFS/Project-X/hardware/krnl_spmv.cpp
~/ProjectFS/Project-X/software/host.cpp
~/ProjectFS/Project-X/cfg/u55c.cfg
```

如果你想看更完整的长文解释，配合这两份一起看：

```text
~/ProjectFS/Project-X/docs/C层到硬件实现_zh.md
~/ProjectFS/Project-X/docs/C层到硬件实现_图解版_zh.md
```

---

## 1. 先记住一张总图

对当前 `krnl_spmv`，整条链可以先压缩成这样：

```text
host.cpp
  -> XRT load_xclbin + 创建 kernel
  -> 给 arg2/arg3/arg4/arg5 传 BO
  -> XRT 写 AXI-Lite control 寄存器
  -> kernel 通过 m_axi 口访问外部内存
  -> link 阶段把这些 m_axi 口接到 U55C HBM[0..3]
```

也就是说：

```text
s_axilite pragma
  决定“控制寄存器和地址寄存器长什么样”

m_axi pragma
  决定“外部内存访存端口长什么样”

PIPELINE / UNROLL pragma
  决定“计算 datapath 和调度方式长什么样”
```

---

## 2. 逐条对照表

| 代码里的 pragma | HLS 生成的东西 | 在 Project-X 里对应什么 | 在 U55C 上大致对应什么 |
|---|---|---|---|
| `#pragma HLS INTERFACE s_axilite port = num_rows bundle = control` | AXI-Lite slave 寄存器项 | `host.cpp` 里 `kernel(rows, ...)` 传入的 `rows` | kernel 控制寄存器里的一个标量字段 |
| `#pragma HLS INTERFACE s_axilite port = scale bundle = control` | AXI-Lite slave 寄存器项 | `host.cpp` 里传入的 `scale` | kernel 控制寄存器里的一个 FP64 标量字段 |
| `#pragma HLS INTERFACE s_axilite port = col_idx bundle = control` | 指针基地址寄存器 | `host.cpp` 里 `col_idx_bo` 的 device address | `gmem_col` 访存口的 base address 来源 |
| `#pragma HLS INTERFACE s_axilite port = values bundle = control` | 指针基地址寄存器 | `values_bo` 的 device address | `gmem_val` 访存口的 base address 来源 |
| `#pragma HLS INTERFACE s_axilite port = x bundle = control` | 指针基地址寄存器 | `x_bo` 的 device address | `gmem_x` 访存口的 base address 来源 |
| `#pragma HLS INTERFACE s_axilite port = y bundle = control` | 指针基地址寄存器 | `y_bo` 的 device address | `gmem_y` 写回口的 base address 来源 |
| `#pragma HLS INTERFACE s_axilite port = return bundle = control` | 标准 HLS 控制寄存器集合 | XRT 启动/等待 kernel | `ap_start/ap_done/ap_idle/ap_ready` 等控制位 |
| `#pragma HLS INTERFACE m_axi port = col_idx offset = slave bundle = gmem_col` | 一组 AXI4 master 信号 | 读 `col_idx[]` | link 后接到 `HBM[0]` |
| `#pragma HLS INTERFACE m_axi port = values offset = slave bundle = gmem_val` | 一组 AXI4 master 信号 | 读 `values[]` | link 后接到 `HBM[1]` |
| `#pragma HLS INTERFACE m_axi port = x offset = slave bundle = gmem_x` | 一组 AXI4 master 信号 | 读 `x[]` | link 后接到 `HBM[2]` |
| `#pragma HLS INTERFACE m_axi port = y offset = slave bundle = gmem_y` | 一组 AXI4 master 信号 | 写 `y[]` | link 后接到 `HBM[3]` |
| `#pragma HLS PIPELINE II=1` | 循环流水线调度 | `RowLoop` 尝试每周期接纳 1 个新 row | 多个 row 迭代在 datapath 中重叠执行 |
| `#pragma HLS LOOP_TRIPCOUNT min=1 max=4096` | 报告用迭代范围提示 | 对应 `host.cpp` 的 `rows` 合法范围 | 主要影响报告估算，不是核心硬件接口 |
| `#pragma HLS UNROLL` | 展开 slot 循环 | `SlotLoop` 的 3 个 slot 尝试并行 | 复制部分计算逻辑，但仍受 AXI/HBM 访存约束 |

---

## 3. `s_axilite`：看起来最“软件”，其实是硬件控制口

代码位置：

```text
~/ProjectFS/Project-X/hardware/krnl_spmv.cpp
```

这几条：

```cpp
#pragma HLS INTERFACE s_axilite port = num_rows bundle = control
#pragma HLS INTERFACE s_axilite port = scale bundle = control
#pragma HLS INTERFACE s_axilite port = col_idx bundle = control
#pragma HLS INTERFACE s_axilite port = values bundle = control
#pragma HLS INTERFACE s_axilite port = x bundle = control
#pragma HLS INTERFACE s_axilite port = y bundle = control
#pragma HLS INTERFACE s_axilite port = return bundle = control
```

可以直接记成：

```text
HLS 给这个 kernel 造了一块 AXI-Lite 控制寄存器空间
```

在当前工程里，它承载三类信息：

1. 标量参数：`num_rows`、`scale`
2. 指针参数的 device base address：`col_idx`、`values`、`x`、`y`
3. kernel 生命周期控制位：`ap_start`、`ap_done`、`ap_idle`、`ap_ready`

和 host 的对应关系是：

```text
host.cpp 调 kernel(...)
  -> XRT 解析参数
  -> 把标量写进 control 寄存器
  -> 把各 BO 的 device address 写进 control 寄存器
  -> 拉起 ap_start
```

所以：

```text
s_axilite 不负责搬大数据
只负责“告诉 kernel 参数是多少、数据从哪读、结果往哪写”
```

---

## 4. `m_axi`：真正搬大块数据的口

这几条：

```cpp
#pragma HLS INTERFACE m_axi port = col_idx offset = slave bundle = gmem_col
#pragma HLS INTERFACE m_axi port = values offset = slave bundle = gmem_val
#pragma HLS INTERFACE m_axi port = x offset = slave bundle = gmem_x
#pragma HLS INTERFACE m_axi port = y offset = slave bundle = gmem_y
```

在 HLS 眼里，意思是：

```text
给这 4 个指针各生成一套 AXI4 master memory interface
```

你可以把每一套 `m_axi` 想成：

```text
一组地址/读写数据/握手信号
+ 一套访问外部内存的请求通路
```

对当前工程：

```text
col_idx -> gmem_col
values  -> gmem_val
x       -> gmem_x
y       -> gmem_y
```

再由 `u55c.cfg` 在 link 阶段指定：

```text
col_idx -> HBM[0]
values  -> HBM[1]
x       -> HBM[2]
y       -> HBM[3]
```

所以落到 U55C 上的大致理解就是：

```text
kernel 里有 4 条独立外存访问通路
平台把它们分别接到 4 个 HBM bank / pseudo-channel
```

这也是为什么当前模板有教学价值：

```text
端口和 bank 是一一分开的，关系很清晰
```

---

## 5. `offset = slave` 到底在帮你做什么

这点很容易被忽略，但很关键。

`offset = slave` 的核心意思是：

```text
这个 m_axi 端口访问外存时，它的 base address 来自 control 寄存器
```

也就是：

```text
host.cpp 里的 BO
  -> XRT 取到 BO 的 device address
  -> 写到 s_axilite 的地址寄存器
  -> m_axi 口再拿这个地址当起点发起访存
```

所以不要把这两类 pragma 分开死记：

```text
s_axilite 和 m_axi 是一套配合关系
一个负责给地址，一个负责拿着地址去访存
```

---

## 6. `PIPELINE II=1`：不是“1 周期算完”，而是“尝试每周期接纳新 row”

对应代码：

```cpp
RowLoop:
for (int row = 0; row < num_rows; ++row) {
#pragma HLS PIPELINE II=1
    ...
}
```

这里最值得避免的误解是：

```text
II=1 != 一行只用 1 个周期
```

它更像是：

```text
目标是让流水线的启动间隔为 1
也就是理想情况下每个周期都能再启动一个新的 row 迭代
```

如果把一个 row 的执行拆成若干 stage，大致会像：

```text
cycle 0: row0 进入 stage A
cycle 1: row0 到 stage B, row1 进入 stage A
cycle 2: row0 到 stage C, row1 到 stage B, row2 进入 stage A
```

对当前 `krnl_spmv`，被流水化的工作大致包含：

1. 读 `col_idx[idx]`
2. 读 `values[idx]`
3. 用 `col` 访问 `x[col]`
4. 做 FP64 乘加
5. 写 `y[row]`

而在 U55C 上，真正限制实际 II 的通常不是“pragma 写得不对”，而是：

1. FP64 运算延迟
2. `x[col]` 的随机访存特性
3. `m_axi` 口的 issue/response 能力
4. HBM bank 返回延迟

所以你应该把 `PIPELINE II=1` 理解成：

```text
对调度器的目标要求
```

而不是：

```text
对真实性能的自动保证
```

---

## 7. `LOOP_TRIPCOUNT`：主要服务报告，不是主要服务 datapath

这条：

```cpp
#pragma HLS LOOP_TRIPCOUNT min=1 max=4096
```

在当前工程里最值得记住的是：

```text
它主要帮助 HLS 在报告里估算 latency / throughput 区间
```

它和 `PIPELINE`、`UNROLL` 不同，不是这段 kernel 的主结构性优化 pragma。

当前 `Project-X` 里：

```text
host.cpp 的 parse_rows() 真正限制 rows 必须在 [1, 4096]
LOOP_TRIPCOUNT 只是把这个范围告诉 HLS
```

---

## 8. `UNROLL`：复制算术路径，不自动创造无限带宽

对应代码：

```cpp
SlotLoop:
for (int slot = 0; slot < kSlotsPerRow; ++slot) {
#pragma HLS UNROLL
    ...
}
```

因为 `kSlotsPerRow = 3` 是编译期常量，所以这里基本可以理解成：

```text
把 3 次 slot 迭代展开成并行硬件
```

比较直观的效果是：

```text
顺序版：
  slot0 算完 -> slot1 算完 -> slot2 算完

展开版：
  HLS 尝试同时准备 slot0 / slot1 / slot2 的逻辑
```

但在当前工程里，必须立刻补上一句更真实的话：

```text
UNROLL 会复制计算逻辑，不会凭空创造额外 HBM 端口
```

也就是说，3 个 slot 虽然被展开了，但它们仍共享：

```text
col_idx -> gmem_col -> HBM[0]
values  -> gmem_val -> HBM[1]
x       -> gmem_x   -> HBM[2]
```

所以最终时序仍要看：

1. HLS 如何调度这些访问
2. AXI 端口一次能发多少请求
3. HBM 返回能否撑住

这个认知非常重要，因为它直接决定你以后看优化时不会掉进一个常见误区：

```text
“写了 UNROLL，就一定变成 N 倍带宽 / N 倍性能”
```

实际并不是。

---

## 9. 当前 `Project-X` 里最该怎么读这些 pragma

如果你只想抓住当前工程里最关键的映射关系，可以记下面这 6 句：

1. `s_axilite` 生成的是控制寄存器，不是搬数据的主通路。
2. 指针参数在 `s_axilite` 侧承载的是 BO 的 device base address。
3. `m_axi` 生成的是外部内存访问端口，真正读写的是 U55C 外部内存。
4. `u55c.cfg` 决定 `m_axi` 端口最终接到哪个 HBM bank。
5. `PIPELINE II=1` 是对 row 级流水线启动间隔的目标要求。
6. `UNROLL` 复制计算逻辑，但不自动复制 HBM 带宽。

---

## 10. 你以后去哪里看“证据”

如果你想把“文档里的说法”和“工具实际生成的东西”对上，优先看这几类输出：

1. `csynth.rpt`
   看控制寄存器、接口摘要、循环 II、资源估算。
2. `solution/impl/verilog/krnl_spmv.v`
   看顶层 RTL 里是否出现 `m_axi_gmem_*`、`s_axi_control_*` 一类端口。
3. link / package 报告
   看 `gmem_col/gmem_val/gmem_x/gmem_y` 最终连到哪些 HBM bank。
4. `u55c.cfg`
   看工程里显式指定的 bank 映射规则。

如果你是第一次看，建议顺序是：

```text
先看 krnl_spmv.cpp 里的 pragma
再看这份对照文档
再去看 csynth.rpt 的 Interface Summary
最后看 u55c.cfg 把端口接到哪
```

---

## 11. 和本工程其他文档的分工

这份文档适合：

```text
我看到一条 pragma，想快速知道它在 U55C 上大概对应什么
```

另外两份更适合：

```text
我想系统理解从 C 到 RTL/HBM/xclbin 的完整链条
```

对应文件：

```text
~/ProjectFS/Project-X/docs/C层到硬件实现_zh.md
~/ProjectFS/Project-X/docs/C层到硬件实现_图解版_zh.md
```
