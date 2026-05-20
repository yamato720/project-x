# HLS 流水线 Demo

这个 demo 放在独立入口中，不改变现有 SpMV 的三种构建变体：

- 共享头文件：`hardware/hls_pipeline_demo.hpp`
- 单 kernel 内部流水 HLS kernel：`hardware/hls/hls_pipeline_demo/krnl_hls_pipeline_demo.cpp`
- 顶层函数级流水 HLS kernel：`hardware/hls/hls_pipeline_demo/krnl_hls_top_pipeline_demo.cpp`
- 多 kernel 级流水 HLS kernel：
  - `hardware/hls/hls_pipeline_demo/krnl_hls_pipeline_source.cpp`
  - `hardware/hls/hls_pipeline_demo/krnl_hls_pipeline_compute.cpp`
  - `hardware/hls/hls_pipeline_demo/krnl_hls_pipeline_sink.cpp`
- 多 kernel stream 共享类型：`hardware/hls_pipeline_stream_common.hpp`
- 专用 host：`software/hls_pipeline_demo_host.cpp`
- kernel 连接配置：`cfg/hls_pipeline_demo.cfg`
- 构建片段：`make/hls_pipeline_demo.mk`
- 根入口：`Makefile.hls_pipeline_demo.mk`

## 覆盖的流水线形式

这个 demo 现在包含三组路径。

第一组是单 XRT kernel 内部流水：

- `krnl_hls_pipeline_demo`

它的 top 函数仍然使用普通 XRT kernel 调用方式。一次 host `run` 启动一次 kernel，完成后 host 取回三组输出并和 CPU golden 对比。

这一组主要展示 HLS `DATAFLOW` 任务流水：

- `load_stage`
- `compute_stage`
- `store_stage`

这三段函数在 top 函数里由 `hls::stream` 连接，并放在 `#pragma HLS DATAFLOW` 区域内。HLS 可以把它们综合成同时工作的任务级流水：load 可以继续读下一拍数据，compute 处理当前数据，store 写出更早的数据。

第二组是单 XRT kernel 顶层函数级流水：

- `krnl_hls_top_pipeline_demo`

这一组把 `#pragma HLS PIPELINE II = 1` 直接放在 top 函数体上，并把内部固定 4 元素 micro-batch 的小循环完全展开。它的目的不是替代 DATAFLOW 版本，而是专门覆盖 HLS schedule 顶层摘要里的 `top_pipelined=yes` 形态。

第三组是真正的多 XRT kernel 级流水：

- `krnl_hls_pipeline_source`
- `krnl_hls_pipeline_compute`
- `krnl_hls_pipeline_sink`

三者在 link 阶段通过 `cfg/hls_pipeline_demo.cfg` 里的 `stream_connect` 连接：

```text
source.output_stream -> compute.input_stream
compute.output_stream -> sink.input_stream
```

host 会按照 `sink -> compute -> source` 的顺序启动三个 kernel，让下游先进入等待状态，再启动上游推流。这样三个 kernel 可以在硬件上同时运行：source 读下一项，compute 处理当前项，sink 写回前一项。

kernel 内部流水线由每个 stage 或每个独立 kernel 自己的循环表达：

- `LoadLoop` 使用 `#pragma HLS PIPELINE II = 1`
- `ComputeLoop` 使用 `#pragma HLS PIPELINE II = 1`
- `StoreLoop` 使用 `#pragma HLS PIPELINE II = 1`
- `LaneLoop` 使用 `#pragma HLS UNROLL`
- `lanes` 使用 `#pragma HLS ARRAY_PARTITION complete`
- `scratch` 使用 `#pragma HLS BIND_STORAGE ... bram`

因此它同时覆盖了后续生成器需要识别的几类形态：任务流水、循环流水、局部并行展开、数组分割、stream FIFO、局部 BRAM、以及寄存器延迟链。

更准确地说：

- 单 kernel 版本覆盖 kernel 内部 `DATAFLOW`。
- 顶层函数级版本覆盖 HLS top schedule 的 `PIPELINE`。
- 三 kernel 版本覆盖硬件 kernel 与 kernel 之间的 stream pipeline。
- 三个版本共用同一套数值公式和 host golden，便于对照它们的行为是否一致。

## 数值意义

这个 demo 的输入生成和硬件计算是分开的。

host 默认先在 CPU 侧生成一段确定性输入数组：

```text
input[i] = 3 * i + 1
```

这个式子不在 FPGA 上计算。host 把生成好的 `input` 数组通过 XRT BO 同步到设备侧内存后，FPGA kernel 才开始连续读取 `input[i]`。

FPGA 上真正执行的是后面的流水计算。对每个元素，compute stage 先构造一个 `base`：

```text
base = input[i] + i
```

然后把 `base` 展开成 4 条 lane。这里的 lane 是为了让 demo 覆盖 `ARRAY_PARTITION complete` 和 `UNROLL`，不是为了实现复杂算法：

```text
lanes = {base + 0, base + 1, base + 2, base + 3}
stage_value[i] = lanes[0] + 2 * lanes[1] - lanes[2] + lanes[3]
```

因为 host 生成的输入是 `input[i] = 3*i+1`，所以这个 demo 中：

```text
base = 4 * i + 1
stage_value[i] = 12 * i + 6
```

`stage_value` 随后经过一个两级寄存器延迟链。注意 `delayed_value[i]` 读取的是更新前的 `delay1`，因此它对应两拍前的 `stage_value`，前两个元素对应初始值 0：

```text
delayed_value[i] = delay1
delay1 = delay0
delay0 = stage_value[i]
output[i] = stage_value[i] + delayed_value[i]
```

等价地说：

```text
delayed_value[0] = 0
delayed_value[1] = 0
delayed_value[i] = stage_value[i - 2]  // i >= 2

output[i] = stage_value[i] + delayed_value[i]
```

例如前几个元素是：

```text
i    input    stage_value    delayed_value    output
0    1        6              0                6
1    4        18             0                18
2    7        30             6                36
3    10       42             18               60
```

所以 `stage_value` 用于观察当前 compute 流水段的组合/展开计算结果，`delayed_value` 用于观察内部寄存器链带来的历史值，`output` 是最终写回 host 的结果。host 的 CPU golden 使用同一套公式逐项校验。

在单 kernel 版本里，`load_stage -> compute_stage -> store_stage` 通过 kernel 内部 FIFO 连续流动。顶层函数级版本固定处理 4 个元素，用来观察 top schedule 的函数级 pipeline。三 kernel 版本里，`source` 从内存连续读 `input[i]`，`compute` 通过 AXI4-Stream 连续接收并计算，`sink` 再通过 AXI4-Stream 接收最终值并写回内存。host 不会每个周期手动喂一个 `i`，而是一次启动 kernel，让 FPGA 自己按 `item_count` 连续跑完整段数组。

## 构建和运行

以下命令都从 `Project-X` 仓库根目录执行。

只编译 host：

```bash
make -f Makefile.hls_pipeline_demo.mk host
```

只编译 HLS xo：

```bash
make -f Makefile.hls_pipeline_demo.mk xo TARGET=sw_emu
```

构建 sw_emu xclbin：

```bash
make -f Makefile.hls_pipeline_demo.mk build TARGET=sw_emu
```

运行 sw_emu。这个命令会先跑单 kernel 内部流水，再跑顶层函数级流水，最后跑三 kernel 级流水：

```bash
make -f Makefile.hls_pipeline_demo.mk run TARGET=sw_emu ITEMS=32
```

加上计时统计：

```bash
make -f Makefile.hls_pipeline_demo.mk run TARGET=sw_emu ITEMS=32 \
  HOST_ARGS="--timing --warmup 1 --repeat 5"
```

这只会重编 demo host，不会重新综合 HLS kernel，也不会重新编译 bitstream。计时输出包含 host 侧分段耗时、单 kernel 版本耗时、顶层函数级 pipeline 版本耗时、三 kernel stream pipeline 整体耗时；如果 XRT/ERT 提供设备侧时间戳，还会输出各个 kernel 的设备侧毫秒数和按 `--kernel-mhz` 换算出的周期数。

手动编译真实硬件 bitstream。Vitis 的真实硬件产物仍是 XRT 加载的 `.xclbin`：

```bash
make -f Makefile.hls_pipeline_demo.mk build-bitstream
```

这条命令等价于固定使用 `TARGET=hw` 构建，默认输出：

```text
build/hls_pipeline_demo/hw/xilinx_u55c_gen3x16_xdma_3_202210_1/hls_pipeline_demo.xclbin
```

运行默认生成的真实硬件 bitstream：

```bash
make -f Makefile.hls_pipeline_demo.mk run-bitstream ITEMS=32 DEVICE_INDEX=0
```

运行真实硬件 bitstream 并打印计时：

```bash
make -f Makefile.hls_pipeline_demo.mk run-bitstream ITEMS=32 DEVICE_INDEX=0 \
  HOST_ARGS="--timing --warmup 1 --repeat 10"
```

如果要运行别处已有的 `.xclbin`，可以覆盖 `BITSTREAM_XCLBIN`：

```bash
make -f Makefile.hls_pipeline_demo.mk run-bitstream \
  BITSTREAM_XCLBIN=/path/to/hls_pipeline_demo.xclbin \
  ITEMS=32 \
  DEVICE_INDEX=0 \
  HOST_ARGS="--timing --repeat 10"
```

计时参数说明：

- `--timing`：打开计时输出。
- `--warmup N`：正式统计前先完整运行 N 次三组 demo。
- `--repeat N`：正式统计运行 N 次，输出 min/avg/max。
- `--kernel-mhz FREQ`：设备侧纳秒时间换算周期时使用的 kernel 频率，默认 `300.300293` MHz。
- `--no-device-timing`：只保留 host 侧墙钟时间，关闭 XRT/ERT 设备侧时间戳读取。

清理这个 demo 的产物：

```bash
make -f Makefile.hls_pipeline_demo.mk clean
```

## 和 XS 等价体的关系

这个 demo 适合用来做 XS/HLS 等价体的第一组样例，因为它把 host 可见的 kernel 启动、kernel 内部 task pipeline、kernel-to-kernel stream pipeline、stage 内部 cycle pipeline 和寄存器延迟链放在同一个很小的例子里。

从 XS 的角度看，可以先把一次 host `run` 视为一次完整 kernel 事务；再把 `load_stage`、`compute_stage`、`store_stage` 映射为 kernel 内部 component；最后用 `stage_value`、`delayed_value`、`output` 三组数组检查事务末尾 host 能读到的结果是否和 HLS/Vitis 一致。

对于三 kernel 版本，则应该把 `source`、`compute`、`sink` 看成三个并列的 simulator/kernel 实体，中间的 AXI4-Stream 是它们之间的端口连接。这个版本更接近“kernel 级流水线”的硬件结构。
