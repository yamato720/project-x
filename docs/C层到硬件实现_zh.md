# Project-X：C 层代码到实际硬件的对应关系

这份文档专门回答一个问题：

```text
我在 C/C++ 里写的东西，最后是怎么变成 U55C 上真实硬件的？
```

这里不讲抽象教科书流程，只讲当前 `Project-X` 这个小型 SpMV demo。

如果你当前最困惑的是：

- `BO` 是什么
- 为什么不用手写起始地址
- 一个 HBM bank 能不能放多个数组

那先去看：

```text
/home/pyx/ProjectFS/Project-X/docs/BO与HBM映射_zh.md
```

---

## 1. 先记一句话

这个工程里有两类 C/C++：

1. `software/host.cpp`
   这是 **host 程序**，运行在 x86 CPU 上，不会被综合成 FPGA 硬件。
2. `hardware/krnl_spmv.cpp`
   这是 **HLS kernel**，会被 Vitis HLS 综合成 RTL，再打包进 `.xo` / `.xclbin`。

所以不要把“整个工程的 C++ 都会变成硬件”理解错了。  
真正变成 RTL 的，是 kernel 那一部分。

---

## 2. 这个工程的完整链路

对当前 demo，可以把流程理解成：

```text
host.cpp
  --g++-->
host.exe

krnl_spmv.cpp
  --v++ -c / HLS-->
Verilog / VHDL RTL
  --打包-->
krnl_spmv.xo

krnl_spmv.xo + cfg/u55c.cfg
  --v++ -l-->
krnl_spmv.xclbin

host.exe + xclbin + XRT
  --> 把 bitstream 和 metadata 装到 U55C
  --> host 通过 PCIe / XRT 调 kernel
  --> kernel 从 HBM 读写数据
```

也就是说：

- `host.cpp` 负责“怎么调用”
- `krnl_spmv.cpp` 负责“硬件怎么算”
- `cfg/u55c.cfg` 负责“这些 AXI 端口接到哪几个 HBM bank”
- `Makefile` 负责把这几个步骤串起来

---

## 3. 哪些文件分别在做什么

### 3.1 `software/host.cpp`

文件：

```text
/home/pyx/ProjectFS/Project-X/software/host.cpp
```

作用：

- 解析命令行参数
- 在 host 侧构造测试矩阵和向量
- 先跑一遍 CPU golden
- 打开 FPGA 设备
- 加载 `xclbin`
- 分配 BO（buffer object）
- 把数据同步到 device
- 启动 kernel
- 把结果读回并和 CPU 对比

这部分是普通 C++ 程序，不会被 HLS 综合。

### 3.2 `hardware/krnl_spmv.cpp`

文件：

```text
/home/pyx/ProjectFS/Project-X/hardware/krnl_spmv.cpp
```

作用：

- 描述 FPGA kernel 的计算内容
- 通过 `#pragma HLS INTERFACE` 指定控制口和访存口
- 通过 `#pragma HLS PIPELINE` / `#pragma HLS UNROLL` 提示 HLS 做流水线和展开

这一部分会被综合成 RTL。

### 3.3 `cfg/u55c.cfg`

文件：

```text
/home/pyx/ProjectFS/Project-X/cfg/u55c.cfg
```

当前内容：

```ini
[connectivity]
sp=krnl_spmv_1.col_idx:HBM[0]
sp=krnl_spmv_1.values:HBM[1]
sp=krnl_spmv_1.x:HBM[2]
sp=krnl_spmv_1.y:HBM[3]
```

这不是算法代码，而是 **kernel 端口到真实 HBM bank 的布线约束**。

### 3.4 `Makefile`

文件：

```text
/home/pyx/ProjectFS/Project-X/Makefile
```

作用：

- `make host` 编 host 程序
- `make build TARGET=sw_emu` 编软件仿真 xclbin
- `make build TARGET=hw` 编真实硬件 xclbin
- `make run-sw` 跑软件仿真
- `make run-hw` 跑真实板卡

---

## 4. 从 C 函数签名到 RTL 顶层端口

当前 kernel 顶层函数是：

```cpp
void krnl_spmv(int num_rows,
               double scale,
               const int* col_idx,
               const double* values,
               const double* x,
               double* y)
```

见：

```text
/home/pyx/ProjectFS/Project-X/hardware/krnl_spmv.cpp
```

这几个参数在 HLS 里不会“自动猜”，而是由 pragma 决定接口类型。

### 4.1 标量参数怎么变

这几行：

```cpp
#pragma HLS INTERFACE s_axilite port = num_rows bundle = control
#pragma HLS INTERFACE s_axilite port = scale bundle = control
...
#pragma HLS INTERFACE s_axilite port = return bundle = control
```

表示：

- `num_rows`
- `scale`
- `col_idx/values/x/y` 的基地址
- `ap_start/ap_done/ap_idle`

都会变成 **AXI-Lite 控制寄存器**。

所以 host 启动 kernel 时，本质上是在通过 XRT 写这些控制寄存器。

在综合报告里可以直接看到这些寄存器：

```text
/home/pyx/ProjectFS/Project-X/build/hw/xilinx_u55c_gen3x16_xdma_3_202210_1/_x_temp/krnl_spmv/krnl_spmv/krnl_spmv/solution/syn/report/csynth.rpt
```

里面能看到：

- `num_rows` 对应 `0x10`
- `scale` 对应 `0x18/0x1c`
- `col_idx/values/x/y` 对应各自 offset 寄存器

### 4.2 指针参数怎么变

这几行：

```cpp
#pragma HLS INTERFACE m_axi port = col_idx offset = slave bundle = gmem_col
#pragma HLS INTERFACE m_axi port = values offset = slave bundle = gmem_val
#pragma HLS INTERFACE m_axi port = x offset = slave bundle = gmem_x
#pragma HLS INTERFACE m_axi port = y offset = slave bundle = gmem_y
```

表示这 4 个指针不会变成片上数组，而是变成 4 个独立的 **AXI4 master memory port**。

最终在 RTL 顶层里，你会看到大量类似：

```text
m_axi_gmem_col_*
m_axi_gmem_val_*
m_axi_gmem_x_*
m_axi_gmem_y_*
```

这些信号已经出现在生成的顶层 RTL 里：

```text
/home/pyx/ProjectFS/Project-X/build/hw/xilinx_u55c_gen3x16_xdma_3_202210_1/_x_temp/krnl_spmv/krnl_spmv/krnl_spmv/solution/impl/verilog/krnl_spmv.v
```

这就是“C 里的一个指针参数”在硬件世界里真正展开后的样子。

---

## 5. 从 `for` 循环到流水线硬件

### 5.1 C 层看到的东西

kernel 主循环是：

```cpp
RowLoop:
for (int row = 0; row < num_rows; ++row) {
#pragma HLS PIPELINE II=1
    ...

SlotLoop:
    for (int slot = 0; slot < kSlotsPerRow; ++slot) {
#pragma HLS UNROLL
        ...
    }

    y[row] = scale * acc;
}
```

这是最关键的一段。

### 5.2 硬件里发生了什么

#### `PIPELINE`

`#pragma HLS PIPELINE II=1` 的意思不是“语法糖”，而是：

- HLS 会尝试把 `RowLoop` 变成一个流水线调度模块
- 尽量做到每 `1` 个时钟周期就接受新一轮迭代

但能不能真的做到 `II=1`，要看：

- 浮点加/乘延迟
- 访存冲突
- 条件分支
- AXI 读写调度

#### `UNROLL`

`SlotLoop` 固定只有 3 次迭代，所以 `#pragma HLS UNROLL` 会让 HLS 尝试把这 3 次操作展开成并行硬件，而不是保留一个 3 次的小顺序循环。

也就是说，从“算法”看仍然是 3 个 slot 相加；  
从“硬件”看更接近：

- 3 路并行取数
- 3 路乘法
- 再做累加

---

## 6. 这个 demo 里流水线有没有真的做出来

有，而且可以从报告里直接验证。

关键报告：

```text
/home/pyx/ProjectFS/Project-X/build/hw/xilinx_u55c_gen3x16_xdma_3_202210_1/_x_temp/krnl_spmv/krnl_spmv/krnl_spmv/solution/syn/report/krnl_spmv_Pipeline_RowLoop_csynth.rpt
```

其中最关键的一行是：

```text
| RowLoop | ... | achieved II = 3 | target II = 1 | ... | Pipelined = yes |
```

意思是：

- 你在 C 里要求 `II=1`
- HLS 确实把 `RowLoop` 做成了流水线
- 但由于当前这个设计的约束，最终实际做到的是 `II=3`

所以结论不是“没流水线”，而是：

```text
有流水线，但没有达到理想的 1-cycle initiation interval。
```

### 6.1 为什么不是 II=1

从当前 kernel 的结构看，至少有几个明显原因：

1. 使用了 FP64 乘法和加法
2. `x[col]` 是按索引随机访问，不是连续流式访问
3. `if (col >= 0)` 让访存与计算路径带条件分支
4. AXI 访存接口本身也会限制调度

综合报告里也能看到一些相关提示，比如：

- `x` 的 load 在条件分支中
- 部分 burst / widening 没法完全推断出来

所以这个 demo 非常适合教学，因为它既能看到流水线成功建立，又能看到“为什么 pragma 不等于结果一定完美”。

---

## 7. 从 C 循环到 RTL 模块名

HLS 不只是“内部调度了一下”，它真的生成了一个对应的 RTL 模块：

```text
krnl_spmv_krnl_spmv_Pipeline_RowLoop
```

可以在这里看到：

```text
/home/pyx/ProjectFS/Project-X/build/hw/xilinx_u55c_gen3x16_xdma_3_202210_1/_x_temp/krnl_spmv/krnl_spmv/krnl_spmv/solution/impl/verilog/krnl_spmv_krnl_spmv_Pipeline_RowLoop.v
```

也就是说：

- C 里的 `RowLoop`
- 不只是逻辑上的“这一段循环”
- 它在硬件里已经对应成了一个有名字的流水线模块

这正是“C 层和实际硬件的联系”最直观的证据。

---

## 8. `host.cpp` 和硬件之间是怎么连上的

虽然 `host.cpp` 不会被综合，但它和硬件的关系非常直接。

### 8.1 `xrt::kernel(...)`

在：

```cpp
auto kernel = xrt::kernel(device, uuid.get(), "krnl_spmv");
```

这里，host 是按 kernel 名字去 `xclbin` 里找到对应 compute unit。

### 8.2 `group_id(arg_index)`

在：

```cpp
xrt::bo bo(device, data.size() * sizeof(T), kernel.group_id(arg_index));
```

这里，host 不是随便分配内存，而是按 kernel 参数所属的 memory group 分配 BO。

然后 `cfg/u55c.cfg` 又进一步把这些 group 接到：

- `HBM[0]`
- `HBM[1]`
- `HBM[2]`
- `HBM[3]`

所以 host -> kernel 参数 -> AXI master -> HBM bank，这条链路是贯通的。

### 8.3 `kernel(rows, scale, col_idx_bo, values_bo, x_bo, y_bo)`

这一句本质上是在做两件事：

1. 把标量参数写到 AXI-Lite 控制寄存器
2. 把 BO 对应的 device 地址写到 `col_idx/values/x/y` 的 base-address 寄存器

然后由 XRT 帮你发起 kernel 执行。

---

## 9. `.xclbin` 到底装了什么

这个工程最后跑的不是单纯 `.bit`，而是：

```text
build/hw/xilinx_u55c_gen3x16_xdma_3_202210_1/krnl_spmv.xclbin
```

它里面不只有 bitstream，还包括：

- kernel metadata
- 接口信息
- memory connectivity
- 平台信息

所以 host 才能只写：

```cpp
device.load_xclbin(xclbin_path);
```

就把：

- 控制寄存器布局
- kernel 名
- buffer 接口
- 目标平台

这些信息一起装载起来。

---

## 10. 一个最简单的对应关系表

| C/C++ 层写法 | 硬件里的对应物 |
| --- | --- |
| `int num_rows`, `double scale` | AXI-Lite 控制寄存器 |
| `const int* col_idx` | AXI4 master 读口 `m_axi_gmem_col_*` |
| `const double* values` | AXI4 master 读口 `m_axi_gmem_val_*` |
| `const double* x` | AXI4 master 读口 `m_axi_gmem_x_*` |
| `double* y` | AXI4 master 写口 `m_axi_gmem_y_*` |
| `for (row...)` | 名为 `Pipeline_RowLoop` 的调度/流水线模块 |
| `#pragma HLS PIPELINE` | 请求 HLS 建立循环流水线 |
| `#pragma HLS UNROLL` | 请求 HLS 复制硬件并行计算 lane |
| `cfg/u55c.cfg` | 端口到 HBM bank 的布线约束 |
| `host.cpp` 的 `xrt::bo` / `kernel(...)` | XRT 侧的 buffer 分配、寄存器写入和 kernel 启动 |

---

## 11. 如果你要继续学，建议怎么观察

最推荐按这个顺序看：

1. `hardware/krnl_spmv.cpp`
   先理解算法和 pragma。
2. `solution/syn/report/krnl_spmv_Pipeline_RowLoop_csynth.rpt`
   看 HLS 最终有没有真的做出流水线、II 是多少。
3. `solution/impl/verilog/krnl_spmv.v`
   看顶层 RTL 端口。
4. `solution/impl/verilog/krnl_spmv_krnl_spmv_Pipeline_RowLoop.v`
   看循环如何被拆成实际模块。
5. `software/host.cpp`
   再回来看 host 是怎么把这些硬件接口调起来的。

这样你会更容易建立这条对应关系：

```text
C 代码
  -> pragma
  -> HLS 调度报告
  -> RTL 模块
  -> xclbin
  -> host/XRT 调用
  -> 板上执行
```

---

## 12. 当前工程里已经验证过的事实

当前这份 `Project-X` demo 已经实测：

```bash
make run-hw ROWS=8 SCALE=2 X0=1 DEVICE_INDEX=0
```

返回：

```text
PASS
```

也就是说，这里讲的这套“C 到硬件”的链路，不是停留在文档层面，而是当前仓库里已经真的跑通了。
