# Project-X：C 层到硬件实现图解版

这份文档是前一份说明的“图解版”。  
如果你更喜欢先看全局结构，再回头看细节，这份会更顺手。

配套详细说明见：

```text
/home/pyx/ProjectFS/Project-X/docs/C层到硬件实现_zh.md
```

如果你是被 `BO / HBM[0] / 起始地址 / 多个数组能不能共 bank` 这些问题卡住，
还要配合看：

```text
/home/pyx/ProjectFS/Project-X/docs/BO与HBM映射_zh.md
```

---

## 1. 一眼看全局

先把这个工程拆成两个世界：

```text
CPU / Host 世界
  host.cpp
  Makefile
  XRT

FPGA / Kernel 世界
  krnl_spmv.cpp
  HLS 生成的 RTL
  HBM bank
  xclbin
```

它们不是混在一起执行，而是通过 `xclbin + XRT` 连接起来。

---

## 2. 编译阶段总图

```text
software/host.cpp
    |
    | g++
    v
build/host.exe


hardware/krnl_spmv.cpp
    |
    | v++ -c
    |   = HLS 综合
    v
RTL (Verilog / VHDL)
    |
    | 打包
    v
krnl_spmv.xo
    |
    | v++ -l + cfg/u55c.cfg
    v
krnl_spmv.xclbin
```

要点：

- `host.cpp` 编出来的是普通 CPU 程序
- `krnl_spmv.cpp` 才会被 HLS 变成 RTL
- `cfg/u55c.cfg` 不参与算法计算，但会决定端口接到哪几个 HBM bank

---

## 3. 运行阶段总图

```text
host.exe 启动
    |
    | 打开 device 0
    v
XRT 连接 U55C
    |
    | load_xclbin()
    v
把 krnl_spmv.xclbin 装到卡上
    |
    | 分配 BO / 同步数据
    v
HBM[0] <- col_idx
HBM[1] <- values
HBM[2] <- x
HBM[3] <- y
    |
    | 启动 kernel
    v
krnl_spmv 在 FPGA 上运行
    |
    | 回读 y
    v
host 对比 CPU golden
```

---

## 4. 文件之间的关系图

```text
Makefile
 ├─ 编 host.cpp -> host.exe
 ├─ 编 krnl_spmv.cpp -> krnl_spmv.xo
 ├─ link krnl_spmv.xo + cfg/u55c.cfg -> krnl_spmv.xclbin
 └─ 运行 host.exe + krnl_spmv.xclbin

cfg/u55c.cfg
 └─ 指定 col_idx/values/x/y 四个端口对应的 HBM bank

host.cpp
 ├─ 生成测试矩阵
 ├─ 生成输入向量 x
 ├─ 生成 CPU golden
 ├─ 分配 XRT BO
 ├─ load_xclbin
 └─ 启动 krnl_spmv

krnl_spmv.cpp
 ├─ 定义 kernel 顶层接口
 ├─ 描述 SpMV 计算
 ├─ 指示 HLS 做 PIPELINE
 └─ 指示 HLS 做 UNROLL
```

---

## 5. `host.cpp` 和 `krnl_spmv.cpp` 的分工图

```text
host.cpp
  做什么：
    - 组织数据
    - 调用 FPGA
    - 校验结果

  不做什么：
    - 不会变成 RTL
    - 不会进 bitstream


krnl_spmv.cpp
  做什么：
    - 描述硬件该怎么算
    - 决定 AXI 接口长什么样
    - 决定循环是否流水线 / 展开

  会变成什么：
    - Verilog / VHDL
    - .xo
    - 最终进 xclbin
```

---

## 6. 从 C 函数参数到硬件端口

当前 kernel 函数：

```cpp
void krnl_spmv(int num_rows,
               double scale,
               const int* col_idx,
               const double* values,
               const double* x,
               double* y)
```

在硬件里会分成两类接口：

### 6.1 控制寄存器

```text
num_rows
scale
各个指针的 base address
start / done / idle
```

来源：

```cpp
#pragma HLS INTERFACE s_axilite ...
```

这类接口最后会出现在 AXI-Lite control 总线上。

### 6.2 外部存储器端口

```text
col_idx  -> m_axi_gmem_col_*
values   -> m_axi_gmem_val_*
x        -> m_axi_gmem_x_*
y        -> m_axi_gmem_y_*
```

来源：

```cpp
#pragma HLS INTERFACE m_axi ...
```

所以：

```text
C 里一个指针
  -> HLS 里一个 memory interface
  -> RTL 里一大组 AXI 信号
```

---

## 7. 从 `for` 循环到流水线模块

这段最关键：

```cpp
RowLoop:
for (int row = 0; row < num_rows; ++row) {
#pragma HLS PIPELINE II=1
    ...
}
```

它在硬件世界里不是“简单按顺序执行的循环”，而会被 HLS 尝试变成：

```text
一个可重叠执行多轮 row 迭代的流水线模块
```

可以把它想成：

```text
时钟周期 0: 处理 row 0 的前半段
时钟周期 1: row 0 继续，row 1 开始
时钟周期 2: row 0 / row 1 / row 2 部分重叠
...
```

当然，前提是：

- 运算延迟允许
- 访存不冲突
- HLS 调度得开

---

## 8. 这个 demo 里的流水线实际结果

不是只写了 pragma，HLS 真的生成了流水线模块。

你可以把关系理解成：

```text
RowLoop (C 标签)
  ->
krnl_spmv_Pipeline_RowLoop (HLS 调度模块名)
  ->
krnl_spmv_krnl_spmv_Pipeline_RowLoop.v (生成的 RTL)
```

对应文件：

```text
/home/pyx/ProjectFS/Project-X/build/hw/xilinx_u55c_gen3x16_xdma_3_202210_1/_x_temp/krnl_spmv/krnl_spmv/krnl_spmv/solution/impl/verilog/krnl_spmv_krnl_spmv_Pipeline_RowLoop.v
```

综合报告里还能看到实际 II：

```text
target II   = 1
achieved II = 3
Pipelined   = yes
```

意思是：

- 目标是每拍启动一轮新 row
- 实际做到了每 3 拍启动一轮
- 所以它确实是流水线，只是没有达到理想 II=1

---

## 9. `UNROLL` 在这个 demo 里是什么意思

内层循环：

```cpp
for (int slot = 0; slot < kSlotsPerRow; ++slot) {
#pragma HLS UNROLL
    ...
}
```

因为 `kSlotsPerRow = 3`，所以这里的意思很直观：

```text
不要保留一个执行 3 次的小循环
而是尽量直接做成 3 路并行的硬件运算
```

也就是：

```text
slot0 的乘法
slot1 的乘法
slot2 的乘法
```

尽量并行算，再汇总到 `acc`。

所以这里体现的是：

- 外层 `PIPELINE`：不同行重叠执行
- 内层 `UNROLL`：同一行的 3 个 slot 尽量并行

---

## 10. HBM 是什么时候接进来的

`krnl_spmv.cpp` 里只写了 bundle 名：

```cpp
bundle = gmem_col
bundle = gmem_val
bundle = gmem_x
bundle = gmem_y
```

真正接到 U55C 的 HBM bank，是 `cfg/u55c.cfg` 决定的：

```ini
sp=krnl_spmv_1.col_idx:HBM[0]
sp=krnl_spmv_1.values:HBM[1]
sp=krnl_spmv_1.x:HBM[2]
sp=krnl_spmv_1.y:HBM[3]
```

所以链路是：

```text
C 指针参数
  -> m_axi bundle 名
  -> cfg/u55c.cfg
  -> 真实 HBM bank
```

---

## 11. 一张“端到端”总图

```text
            ┌────────────────────┐
            │ software/host.cpp  │
            │  CPU 上运行的程序   │
            └─────────┬──────────┘
                      │ g++
                      v
            ┌────────────────────┐
            │   build/host.exe   │
            └─────────┬──────────┘
                      │
                      │ XRT 调用
                      │
                      v
            ┌────────────────────┐
            │  krnl_spmv.xclbin  │
            │ bitstream+metadata │
            └─────────┬──────────┘
                      │
                      │ 来自 v++ -l
                      │
            ┌─────────┴──────────┐
            │                    │
            v                    v
   ┌──────────────────┐  ┌──────────────────┐
   │hardware/krnl_spmv.cpp│ │   cfg/u55c.cfg   │
   │   HLS kernel C   │  │ HBM 连接约束文件 │
   └────────┬─────────┘  └────────┬─────────┘
            │ v++ -c / HLS         │
            v                      │
   ┌──────────────────┐            │
   │ Verilog / VHDL   │<───────────┘
   │  + AXI 接口       │
   │  + Pipeline RTL  │
   └────────┬─────────┘
            │
            v
   ┌──────────────────┐
   │   FPGA on U55C   │
   │  访问 HBM 计算 y  │
   └──────────────────┘
```

---

## 12. 读这份图解时最容易记住的三句话

### 12.1 第一句

```text
host.cpp 不会变成硬件，krnl_spmv.cpp 才会。
```

### 12.2 第二句

```text
C 里的指针参数，最后会展开成一大组 AXI memory port 信号。
```

### 12.3 第三句

```text
PIPELINE 和 UNROLL 不是抽象概念，它们会对应到真实生成的 RTL 模块和综合报告里的 II / Latency。
```

---

## 13. 建议怎么配合上一份文档读

推荐顺序：

1. 先看这份图解版，建立全局脑图
2. 再看：

```text
/home/pyx/ProjectFS/Project-X/docs/C层到硬件实现_zh.md
```

3. 最后边看源码边对照：

- `software/host.cpp`
- `hardware/krnl_spmv.cpp`
- `cfg/u55c.cfg`
- `solution/syn/report/*.rpt`
- `solution/impl/verilog/*.v`

这样就能把“概念 -> 代码 -> RTL -> 报告 -> 板上运行”串起来。
