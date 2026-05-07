# 运行已有 xclbin 与 host 计时

本文说明两类运行入口：

- `run-*`：按 Makefile 依赖先保证 host 和 xclbin 是最新的，再运行。
- `run-*-existing`：只编译 host，不重新生成 `.xo` / `.xclbin`，直接加载已有 xclbin 运行。

## 为什么加 existing 入口

原来的 `run-sw` / `run-hw` 都依赖 `$(XCLBIN)`：

```makefile
run: host $(XCLBIN)
```

这不是“无条件重编”，而是 Makefile 根据依赖时间戳判断是否需要重建。但对硬件来说，只要 HLS 源码、配置、Chisel 生成文件或 IP 文件比 xclbin 新，`make run-hw` 就会重新触发 `v++ -c` / `v++ -l`，代价很高。

因此新增 `run-existing`，只依赖 host：

```makefile
run-existing: host
```

它会先检查指定的 xclbin 是否存在，然后直接调用 host。硬件路径下不会调用 `v++`。

## 直接运行已有 sw_emu xclbin

默认路径：

```bash
make run-sw-existing VARIANT=hybrid ROWS=8 SCALE=2 X0=1
```

默认使用：

```text
build/hybrid/sw_emu/xilinx_u55c_gen3x16_xdma_3_202210_1/krnl_spmv.xclbin
```

软件仿真仍需要 `emconfig.json`。如果它已经存在，不会重新生成；如果不存在，Makefile 只会调用 `emconfigutil` 生成仿真配置，不会重新综合 kernel。

也可以显式指定 xclbin：

```bash
make run-sw-existing VARIANT=hybrid XCLBIN_PATH=/path/to/krnl_spmv.xclbin ROWS=8 SCALE=2 X0=1
```

## 直接运行已有硬件 xclbin

默认路径：

```bash
make run-hw-existing VARIANT=hybrid ROWS=8 SCALE=2 X0=1
```

默认使用：

```text
build/hybrid/hw/xilinx_u55c_gen3x16_xdma_3_202210_1/krnl_spmv.xclbin
```

这个目标不会构建 `.xo`，也不会构建 `.xclbin`。如果默认路径下没有硬件 xclbin，会报错并提示用 `XCLBIN_PATH` 指定：

```bash
make run-hw-existing VARIANT=hybrid XCLBIN_PATH=/path/to/hw.xclbin ROWS=8 SCALE=2 X0=1
```

## host 侧计时

host 新增三个参数：

```text
--timing
--warmup N
--repeat N
```

Makefile 对应变量：

```bash
make run-hw-existing VARIANT=hybrid ROWS=8 SCALE=2 X0=1 TIMING=1 WARMUP=1 REPEAT=5
```

含义：

- `TIMING=1`：打印 host 分段耗时。当前 Makefile 默认就是 `TIMING=1`。
- `WARMUP=N`：正式计时前先运行 N 次 kernel，不计入 kernel min/avg/max。
- `REPEAT=N`：正式运行 N 次 kernel，并统计 kernel 提交到 `wait()` 返回的 min/avg/max。

如果你只想看功能正确性、不打印计时，可以显式关闭：

```bash
make run-hw-existing VARIANT=hybrid ROWS=8 SCALE=2 X0=1 TIMING=0
```

输出字段：

```text
Timing ms:
  prepare_cpu=...
  xrt_setup=...
  buffer_h2d=...
  kernel_min=...
  kernel_avg=...
  kernel_max=...
  buffer_d2h=...
  verify=...
  total=...
```

这些时间是 host 视角耗时，不是硬件内部 cycle counter。`kernel_*` 统计的是 host 启动 kernel 到 `run.wait()` 返回之间的 wall-clock 时间，包含 XRT 调度开销。

## 哪些目标会重新构建

会按依赖构建 xclbin：

```bash
make run-sw
make run-hw
make build TARGET=sw_emu
make build TARGET=hw VARIANT=hybrid
```

不会构建 xclbin：

```bash
make run-sw-existing
make run-hw-existing
```

如果你同时保留三种变体的产物，应该显式带上 `VARIANT=`，否则默认会落到 `hybrid`。

如果只是想反复测同一个硬件 bitstream 的 host 行为和耗时，优先用：

```bash
make run-hw-existing VARIANT=hybrid TIMING=1 WARMUP=1 REPEAT=5
```
