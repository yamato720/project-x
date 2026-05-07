# Project-X 构建变体

当前 Makefile 支持三种 kernel 变体。三种变体的 host 程序、XRT 调用方式、kernel 顶层函数名和参数顺序保持一致；区别只在硬件 kernel 的 C++ 源文件和是否接入 Chisel/Vivado RTL black-box。

## 变体对照

```text
VARIANT=hls
  源文件：hardware/hls/krnl_spmv.cpp
  SpMV：HLS C++ double 乘加
  外积：HLS C++ double 乘法
  Chisel 依赖：无

VARIANT=hybrid
  源文件：hardware/hybrid/krnl_spmv.cpp
  SpMV：HLS C++ double 乘加
  外积：outer_product_mul_bits Chisel/Vivado FP64 black-box
  Chisel 依赖：generated/outer

VARIANT=chisel_core
  源文件：hardware/chisel_core/krnl_spmv.cpp
  SpMV：spmv_row_muladd_bits Chisel/Vivado FP64 black-box
  外积：outer_product_mul_bits Chisel/Vivado FP64 black-box
  Chisel 依赖：generated/outer + generated/spmv_row
```

这三个源文件都导出同一个顶层函数：

```cpp
extern "C" void krnl_spmv(...)
```

所以 `software/host.cpp` 不需要知道具体变体；Makefile 在 `v++ -c` 时选择对应的 `.cpp`。

## 常用参数

在根目录 `make` 命令里，经常会看到下面这些参数：

```text
VARIANT       选择 kernel 版本：hls / hybrid / chisel_core
ROWS          测试矩阵行数
SCALE         SpMV 前面的标量 scale
X0            输入向量起始值，host 会生成 x[i] = X0 + i
DEVICE_INDEX  选择第几张 FPGA 卡，默认 0
TIMING=1      打印 host 侧计时；当前默认就是 1
WARMUP=N      正式计时前预热 N 次
REPEAT=N      正式计时重复运行 N 次
BUILD_MODE    顺序构建脚本模式：sw / hw / both
TEST_MODE     顺序测试脚本模式：sw / hw / both
PACKAGE_MODE  顺序打包脚本模式：min / full / both
```

## 构建命令

分别生成软件仿真 xclbin：

```bash
make build TARGET=sw_emu VARIANT=hls
make build TARGET=sw_emu VARIANT=hybrid
make build TARGET=sw_emu VARIANT=chisel_core
make build-sw-chisel-core
```

一次顺序生成三种软件仿真版本：

```bash
make build-all-sw
```

分别生成硬件 bitstream：

```bash
make build TARGET=hw VARIANT=hls
make build TARGET=hw VARIANT=hybrid
make build TARGET=hw VARIANT=chisel_core
make build-hw-chisel-core
```

一次顺序生成三种硬件版本：

```bash
make build-all-hw
```

硬件构建耗时很长，实际使用时也可以放到 tmux：

```bash
make tmux-build TARGET=hw VARIANT=chisel_core
```

## 输出路径

构建产物按变体隔离：

```text
build/hls/sw_emu/<device>/krnl_spmv.xclbin
build/hybrid/sw_emu/<device>/krnl_spmv.xclbin
build/chisel_core/sw_emu/<device>/krnl_spmv.xclbin

build/hls/hw/<device>/krnl_spmv.xclbin
build/hybrid/hw/<device>/krnl_spmv.xclbin
build/chisel_core/hw/<device>/krnl_spmv.xclbin
```

辅助产物也按变体隔离：

```text
build/hls/host.exe
build/hybrid/host.exe
build/chisel_core/host.exe

logs/vpp/<variant>/<target>/<device>/
logs/vivado/<variant>/
build/packages/<variant>/
build/<variant>/open_routed_vivado.tcl
```

报告和 Vitis/Vivado 中间日志也按变体隔离：

```text
reports/<variant>/<target>/<device>/
logs/vpp/<variant>/<target>/<device>/
```

Vivado GUI 打包/下载也要带同一个变体：

```bash
make vivado-package VARIANT=hybrid
make vivado-package-full VARIANT=chisel_core
PROJECT_X_VARIANT=chisel_core scripts/download-vivado-view.sh USER@SERVER ./project-x-vivado --min
```

## 运行命令

构建并运行软件仿真：

```bash
make run-sw VARIANT=hls ROWS=8 SCALE=2 X0=1
make run-sw VARIANT=hybrid ROWS=8 SCALE=2 X0=1
make run-sw VARIANT=chisel_core ROWS=8 SCALE=2 X0=1
```

运行已有硬件 xclbin，不触发重新综合：

```bash
make run-hw-existing VARIANT=hls ROWS=8 SCALE=2 X0=1
make run-hw-existing VARIANT=hybrid ROWS=8 SCALE=2 X0=1
make run-hw-existing VARIANT=chisel_core ROWS=8 SCALE=2 X0=1
```

也可以用短目标：

```bash
make run-sw-hls
make run-sw-hybrid
make run-sw-chisel-core
```

## 顺序脚本

项目根目录提供了两个顺序脚本：

```bash
scripts/build-all-variants.sh sw
scripts/build-all-variants.sh hw
scripts/build-all-variants.sh both
```

它会依次执行：

```text
hls -> hybrid -> chisel_core
```

如果希望继续从 `make` 入口调用，也有一个薄封装目标：

```bash
make build-all-variants BUILD_MODE=hw
```

两者关系是：

```text
Makefile 里的 build-all-sw / build-all-hw 适合做简单顺序调用。
scripts/build-all-variants.sh 适合做长流程编排和总日志记录。
make build-all-variants 只是对脚本的一个薄封装。
```

测试脚本：

```bash
scripts/test-all-variants.sh sw 8 2 1
scripts/test-all-variants.sh hw 8 2 1
scripts/test-all-variants.sh both 8 2 1
```

测试脚本默认调用 `run-sw-existing` / `run-hw-existing`，适合在产物已经存在时做顺序回归，不会额外触发硬件重编。

测试完成后，脚本还会额外打印一段：

```text
Timing Summary
```

把三种 variant 的关键时间字段集中到一起，方便直接比较。

同样也有 `make` 薄封装入口：

```bash
make test-all-variants TEST_MODE=sw ROWS=8 SCALE=2 X0=1
```

打包脚本：

```bash
scripts/package-all-variants.sh min
scripts/package-all-variants.sh full
scripts/package-all-variants.sh both
```

对应的 `make` 薄封装入口：

```bash
make vivado-package-all PACKAGE_MODE=both
```

## 当前边界

`chisel_core` 不是完整 RTL kernel packaging。它仍然让 HLS 负责 XRT/AXI-Lite/HBM `m_axi` 外壳，Chisel/Vivado 接管行内 SpMV FP64 乘加和外积 FP64 乘法。

如果以后要让 Chisel 自己发 AXI master 读写 HBM，那会进入完整 RTL kernel 或更复杂 black-box AXI 接口路线，Makefile 和 kernel packaging 都要再扩展。
