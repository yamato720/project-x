# Project-X: U55C SpMV 入门模板

更新时间：2026-04-28

这个目录是一个小型 U55C/Vitis/XRT 模板。它不是追求性能的 SpMV 实现，而是用一个足够小的稀疏矩阵向量乘法，让入门者能看清：

```text
host 生成输入
  -> XRT 分配 device buffer
  -> host 把数据搬到 U55C HBM
  -> HLS kernel 自动生成流水线
  -> kernel 从 HBM 读稀疏矩阵和向量
  -> kernel 写回 y
  -> host 读回并和 CPU reference 对比
```

当前计算：

```text
y = scale * A * x
```

其中 host 生成一个三对角稀疏矩阵：

```text
A[i, i-1] = -1
A[i, i]   =  4
A[i, i+1] = -1
x[i]      = x0 + i
```

稀疏格式使用简化的 ELLPACK：

```text
每行固定 3 个 slot
无效 slot 使用 col_idx = -1, value = 0
```

这种格式比 CSR 更适合入门看流水线，因为 kernel 每一行工作量固定，`slot` 循环能直接 `UNROLL`。

## 1. 哪些文件和架构有关

不只有 `src/krnl_spmv.cpp`。

```text
src/krnl_spmv.cpp   HLS kernel，本体计算、AXI port、PIPELINE/UNROLL 都在这里
cfg/u55c.cfg        把 kernel 的 AXI master port 映射到 U55C HBM bank
src/host.cpp        host/XRT 程序，决定参数顺序、buffer 分配、数据搬运、结果读回
Makefile            决定 Vitis target、platform、kernel 名、xclbin 路径
```

真正上 U55C 时，这四层合起来才是完整设计。

## 2. 文件结构

```text
Project-X/
  Makefile
  cfg/u55c.cfg
  src/host.cpp
  src/krnl_spmv.cpp
  README_zh.md
```

## 3. 环境

推荐先加载 Vitis 2022.2 和 XRT：

```bash
export XILINX_VITIS=/tools/Xilinx2022/Vitis/2022.2
export XILINX_VIVADO=/tools/Xilinx2022/Vivado/2022.2
export XILINX_HLS=/tools/Xilinx2022/Vitis_HLS/2022.2
export PATH=/tools/Xilinx2022/Vitis/2022.2/bin:/tools/Xilinx2022/Vitis_HLS/2022.2/bin:/tools/Xilinx2022/Vivado/2022.2/bin:$PATH
source /opt/xilinx/xrt/setup.sh

cd /home/pyx/ProjectFS/Project-X
```

## 4. 软件仿真

先跑 `sw_emu`，验证 host、kernel 参数、xclbin 加载和结果对比：

```bash
make run TARGET=sw_emu ROWS=8 SCALE=2 X0=1
```

参数含义：

```text
ROWS   矩阵行数，也是向量长度
SCALE  kernel 里的标量参数
X0     host 生成 x[i] = X0 + i
```

Makefile 会自动生成 `emconfig.json`，并在运行时设置 `EMCONFIG_PATH` 和 `XCL_EMULATION_MODE`。

期望能看到类似：

```text
Running krnl_spmv(rows=8, scale=2, x0=1)
First rows:
  y[0] fpga=4 cpu=4
  y[1] fpga=8 cpu=8
  ...
max abs diff: 0
PASS
```

## 5. 看 HLS 自动流水线

核心代码在：

```bash
/home/pyx/ProjectFS/Project-X/src/krnl_spmv.cpp
```

关键 pragma：

```cpp
#pragma HLS PIPELINE II=1
#pragma HLS UNROLL
```

含义：

```text
PIPELINE II=1
  请求 HLS 尽量让 RowLoop 的不同迭代重叠执行。
  实际 II 要看 HLS 报告，可能受 FP64、随机 x[col] 读、AXI 调度影响。

UNROLL
  因为每行固定 3 个 slot，请求 HLS 把 3 个 slot 的计算展开成并行硬件。
```

软件里这只是双重 for-loop；HLS 会把它收敛成控制 FSM、AXI master、浮点乘加路径和流水线寄存器。

编译后可以从报告里找 II：

```bash
find /home/pyx/ProjectFS/Project-X/reports -type f | rg 'csynth|summary|rpt'
```

## 6. U55C HBM 连接

`cfg/u55c.cfg` 当前写法：

```ini
[connectivity]
sp=krnl_spmv_1.col_idx:HBM[0]
sp=krnl_spmv_1.values:HBM[1]
sp=krnl_spmv_1.x:HBM[2]
sp=krnl_spmv_1.y:HBM[3]
```

也就是说：

```text
col_idx  放 HBM[0]
values   放 HBM[1]
x        放 HBM[2]
y        放 HBM[3]
```

这比 `a*b+c` 多了真实的多 HBM port 连接，但仍然足够小，不容易踩到 U55C HBM switch 端口耗尽问题。

## 7. 编译真实 U55C bitstream

真实硬件编译会很久，建议用 tmux：

```bash
make tmux-build TARGET=hw
```

模板默认把 `HLS_JOBS` 和 `VIVADO_JOBS` 设成 2，适合和其他编译任务共存。如果机器空闲，可以手动提高：

```bash
make tmux-build TARGET=hw HLS_JOBS=8 VIVADO_JOBS=8
```

查看：

```bash
tmux attach -t project-x-u55c-build
```

从 tmux 安全退出但不停止编译：

```text
Ctrl-b
d
```

也可以直接前台跑：

```bash
make build TARGET=hw
```

生成文件：

```bash
/home/pyx/ProjectFS/Project-X/build/hw/xilinx_u55c_gen3x16_xdma_3_202210_1/krnl_spmv.xclbin
```

## 8. 上板运行

硬件 xclbin 编好后：

```bash
cd /home/pyx/ProjectFS/Project-X
source /opt/xilinx/xrt/setup.sh
xbutil examine
make run TARGET=hw ROWS=8 SCALE=2 X0=1
```

如果要换设备编号：

```bash
make run TARGET=hw ROWS=8 SCALE=2 X0=1 DEVICE_INDEX=1
```

## 9. 已验证状态

当前已经验证：

```bash
make run TARGET=sw_emu ROWS=8 SCALE=2 X0=1
```

真实 U55C `hw` bitstream 没有在这里启动，避免和当前 `pcg` 的长时间 bitstream 编译抢资源。

## 10. 后续怎么改

可以先改：

```bash
/home/pyx/ProjectFS/Project-X/src/krnl_spmv.cpp
```

比如把每行 slot 数从 3 改大，或者把三对角矩阵换成更一般的 ELLPACK 输入。

如果改了 kernel 参数列表，必须同步改：

```text
src/host.cpp      xrt::kernel 调用顺序和 group_id
cfg/u55c.cfg      sp=... 端口名
Makefile          kernel 名和输出文件名
```
