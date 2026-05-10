# Project-X

`Project-X` 现在分成三块：

- `software/`
  host/XRT 程序。
- `hardware/`
  HLS kernel 和 Chisel 模块。
- `docs/`
  中文讲解文档与索引。
- `Project-XPlus/`
  Jacobi-PCG 多 kernel HLS/XRT 子工程，现已独立为 submodule。

## 目录

```text
Project-X/
  Makefile
  cfg/
  docs/
  Project-XPlus/
  hardware/
    chisel/
    krnl_spmv_common.hpp
    hls/krnl_spmv.cpp
    hybrid/krnl_spmv.cpp
    chisel_core/krnl_spmv.cpp
  software/
    host.cpp
```

## 快速入口

- 文档索引：[docs/README.md](~/ProjectFS/Project-X/docs/README.md)
- `Project-XPlus` 子工程：[Project-XPlus/README.md](~/ProjectFS/Project-X/Project-XPlus/README.md)
- Kernel 变体：
  [hls/krnl_spmv.cpp](~/ProjectFS/Project-X/hardware/hls/krnl_spmv.cpp)、
  [hybrid/krnl_spmv.cpp](~/ProjectFS/Project-X/hardware/hybrid/krnl_spmv.cpp)、
  [chisel_core/krnl_spmv.cpp](~/ProjectFS/Project-X/hardware/chisel_core/krnl_spmv.cpp)
- Chisel 模块说明：[hardware/chisel/README.md](~/ProjectFS/Project-X/hardware/chisel/README.md)
- host 程序：[software/host.cpp](~/ProjectFS/Project-X/software/host.cpp)

## 文档目录

怎么跑和调试：

- [构建变体_zh.md](~/ProjectFS/Project-X/docs/构建变体_zh.md)
- 说明 `hls` / `hybrid` / `chisel_core` 三种 kernel 变体如何仿真、生成 xclbin 和查看输出路径。
- [运行已有xclbin与host计时_zh.md](~/ProjectFS/Project-X/docs/运行已有xclbin与host计时_zh.md)

代码研究：

- [C++模板与make_bo详解_zh.md](~/ProjectFS/Project-X/docs/C++模板与make_bo详解_zh.md)

硬件接口与资源：

- [BO与HBM映射_zh.md](~/ProjectFS/Project-X/docs/BO与HBM映射_zh.md)

Chisel 与 HLS 集成：

- [HLS与Chisel取舍_zh.md](~/ProjectFS/Project-X/docs/HLS与Chisel取舍_zh.md)
- [HLS外壳_Chisel计算核_zh.md](~/ProjectFS/Project-X/docs/HLS外壳_Chisel计算核_zh.md)
- [多BlackBox共存问题_zh.md](~/ProjectFS/Project-X/docs/多BlackBox共存问题_zh.md)
- [hardware/chisel/README.md](~/ProjectFS/Project-X/hardware/chisel/README.md)
- [hardware/chisel/安装与版本切换_zh.md](~/ProjectFS/Project-X/hardware/chisel/安装与版本切换_zh.md)

旧版本文档归档：

- [docs/archive/README.md](~/ProjectFS/Project-X/docs/archive/README.md)

## 构建变体

当前 Makefile 支持三种 kernel 变体：

```text
hls          纯 HLS：SpMV 和外积都由 HLS C++ 生成硬件
hybrid       默认：SpMV 用 HLS，外积 FP64 乘法用 Chisel/Vivado RTL black-box
chisel_core  HLS 做 XRT/AXI/HBM 外壳，SpMV 行内乘加和外积乘法用 Chisel/Vivado RTL black-box
```

软件仿真可以分别生成三份 xclbin：

```bash
cd ~/ProjectFS/Project-X
make build TARGET=sw_emu VARIANT=hls
make build TARGET=sw_emu VARIANT=hybrid
make build TARGET=sw_emu VARIANT=chisel_core
```

或者一次顺序生成三种软件仿真版本：

```bash
make build-all-sw
```

硬件 bitstream 同理：

```bash
make build TARGET=hw VARIANT=hls
make build TARGET=hw VARIANT=hybrid
make build TARGET=hw VARIANT=chisel_core
```

或者一次顺序生成三种硬件版本：

```bash
make build-all-hw
```

输出路径按变体隔离，例如：

```text
build/hls/hw/xilinx_u55c_gen3x16_xdma_3_202210_1/krnl_spmv.xclbin
build/hybrid/hw/xilinx_u55c_gen3x16_xdma_3_202210_1/krnl_spmv.xclbin
build/chisel_core/hw/xilinx_u55c_gen3x16_xdma_3_202210_1/krnl_spmv.xclbin
```

辅助产物也按变体隔离：

```text
build/hls/host.exe
build/hybrid/host.exe
build/chisel_core/host.exe

reports/<variant>/<target>/<device>/
logs/vpp/<variant>/<target>/<device>/
logs/vivado/<variant>/
build/packages/<variant>/
```

详细说明见：[构建变体_zh.md](~/ProjectFS/Project-X/docs/构建变体_zh.md)

## 常用命令

这些命令里最常见的参数含义如下：

```text
VARIANT       选择 kernel 版本：hls / hybrid / chisel_core
ROWS          测试矩阵的行数，也是向量 x/y 的长度
SCALE         kernel 里的标量 scale，最终计算 y = scale * A * x
X0            host 生成输入向量时的起始值，x[i] = X0 + i
DEVICE_INDEX  选择第几张 FPGA 卡，默认 0
TIMING=1      打印 host 侧计时；当前默认就是 1
WARMUP=N      正式计时前先预热 N 次
REPEAT=N      正式计时重复运行 N 次并统计 min/avg/max
BUILD_MODE    一键构建脚本模式：sw / hw / both
TEST_MODE     一键测试脚本模式：sw / hw / both
PACKAGE_MODE  一键打包脚本模式：min / full / both
```

软件仿真：

```bash
cd ~/ProjectFS/Project-X
make run-sw VARIANT=hybrid ROWS=8 SCALE=2 X0=1
```

只运行已有软件仿真产物，不触发 xclbin 重新构建：

```bash
cd ~/ProjectFS/Project-X
make run-sw-existing VARIANT=hybrid ROWS=8 SCALE=2 X0=1
```

硬件 bitstream：

```bash
cd ~/ProjectFS/Project-X
make build TARGET=hw VARIANT=hybrid
```

只运行已有硬件 xclbin，不触发硬件重新构建。当前默认就会打印 host 侧计时：

```bash
cd ~/ProjectFS/Project-X
make run-hw-existing VARIANT=hybrid ROWS=8 SCALE=2 X0=1
```

host 侧计时当前默认开启。如果想关闭，显式传 `TIMING=0`：

```bash
cd ~/ProjectFS/Project-X
make run-hw-existing VARIANT=hybrid ROWS=8 SCALE=2 X0=1 TIMING=1 WARMUP=1 REPEAT=5
make run-hw-existing VARIANT=hybrid ROWS=8 SCALE=2 X0=1 TIMING=0
```

生成 Chisel Verilog：

```bash
cd ~/ProjectFS/Project-X
make chisel
```

顺序生成三种版本：

```bash
bash scripts/run-build-with-log.sh hw chisel_core
make build-logged TARGET=hw VARIANT=chisel_core
scripts/build-all-variants.sh sw
scripts/build-all-variants.sh hw
make build-all-variants BUILD_MODE=hw
make build-hw-chisel-core
```

顺序测试三种版本：

```bash
scripts/test-all-variants.sh sw 8 2 1
scripts/test-all-variants.sh hw 8 2 1
make test-all-variants TEST_MODE=sw ROWS=8 SCALE=2 X0=1
```

顺序测试脚本在每种 variant 的原始输出之后，还会额外打印一段 `Timing Summary`，
把 `kernel_min / kernel_avg / kernel_max / buffer_h2d / total` 集中排在一起，方便横向比较。

顺序打包三种版本：

```bash
scripts/package-all-variants.sh both
make vivado-package-all PACKAGE_MODE=both
```

根目录日志说明：

```text
logs/build_history.log
  记录每次单独构建的历史，最新一条在最前面。

logs/build_all_variants_*.log
  记录一次顺序构建批次的汇总，最新条目也会插到最前面。

logs/build_<variant>_<target>_<timestamp>.log
  记录某一次具体构建的完整终端输出。
```

打开 Vivado 工程或最终 routed checkpoint：

```bash
cd ~/ProjectFS/Project-X
make vivado-open
make vivado-routed
```

默认 Vivado GUI 缩放是 200%。如果需要改成 100%：

```bash
make vivado-open VIVADO_SCALE=1
```

如果要通过 SSH 下载到本地 Vivado 查看，优先下载 `level0_wrapper_routed.dcp` 和 `reports/<variant>/hw/...`；如果要看完整 Vitis/Vivado 工程结构，下载整个 `build/<variant>/hw/.../_x_temp/link/vivado/vpl/`。服务器端可用 `make vivado-package VARIANT=hybrid` / `make vivado-package-full VARIANT=hybrid` 打包，Linux/macOS 客户端可用 [download-vivado-view.sh](~/ProjectFS/Project-X/scripts/download-vivado-view.sh)，Windows 客户端可用 [download-vivado-view.ps1](~/ProjectFS/Project-X/scripts/download-vivado-view.ps1)。细节见：[archive/Vivado设计查看教程_zh.md](~/ProjectFS/Project-X/docs/archive/Vivado设计查看教程_zh.md)

## 当前 HLS 输出

三种 `hardware/krnl_spmv_*.cpp` 变体都会输出两份结果：

- `y = scale * A * x`
- `yy_t = y * y^T`

这里的 `yy_t` 是完整外积矩阵，不是只算上三角。  
因为输出规模是 `rows * rows`，host 侧目前把 `rows` 限制在 `512` 以内。

## 当前 Chisel 范围

`hardware/chisel` 当前使用 Chisel `7.11.0`、Scala `2.13.18`、sbt `1.11.7`，默认通过 OpenJDK 17 生成。

`hardware/chisel` 里现在有这些模块：

- `MergeReducer`
  把多个 lane 的部分结果归并求和。
- `SpmvRowEngine`
  生成单行 `SpMV` 的乘加骨架，并复用 `MergeReducer` 做行内 reduction。
- `OuterProductMul`
  为 HLS 外积阶段生成 RTL black-box wrapper、C model、JSON 描述和 Vivado FP64 multiply IP，让 `make run-sw` 在构建 `krnl_spmv.xo` 前先跑 Chisel/Vivado 生成。
- `SpmvRowMulAdd`
  为 `chisel_core` 变体生成 SpMV 单行 FP64 乘加 RTL black-box，把 `scale * (val0*x0 + val1*x1 + val2*x2)` 从 HLS compute datapath 下沉到 Chisel/Vivado IP。

当前实现是整数 datapath 结构模板，不是 FP64 等价实现。  
`OuterProductMul` 已经把外积乘法换成 Xilinx Floating Point FP64 IP；软件仿真路径仍保留 C++ fallback/model 保持 `double` 结果一致。
`SpmvRowMulAdd` 是新增的 FP64 行计算 black-box，用于 `VARIANT=chisel_core`。

## 一个关键澄清

`SpMV` 输出的是 `y = A * x`，这本身不能直接推出矩阵行列式。  
归并模块适合做部分和归约、dot-product 汇总、stream merge；它不能单独替代矩阵分解算法。
