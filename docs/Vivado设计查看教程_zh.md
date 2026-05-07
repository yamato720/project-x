# Vivado 设计查看教程

本文说明如何在已经生成硬件 bitstream 后，用 Vivado 查看 `Project-X` 的模块层级、框图、资源占用、SLR/器件分布和时序报告。

当前工程支持两种打开方式：

```bash
make vivado-open
make vivado-routed
```

两者都适合 SSH + X11 转发场景，并默认用 200% GUI 缩放。

## 前提

先要有硬件构建产物：

```bash
make build TARGET=hw
```

当前默认平台的 Vivado 工程位置是：

```text
build/hw/xilinx_u55c_gen3x16_xdma_3_202210_1/_x_temp/link/vivado/vpl/prj/prj.xpr
```

最终 routed checkpoint 位置是：

```text
build/hw/xilinx_u55c_gen3x16_xdma_3_202210_1/_x_temp/link/vivado/vpl/prj/prj.runs/impl_1/level0_wrapper_routed.dcp
```

如果是远程机器，建议这样连：

```bash
ssh -Y <server>
```

`make vivado-open` 和 `make vivado-routed` 会检查 `DISPLAY`。如果 `DISPLAY` 为空，说明当前 shell 没有 X11 转发。

## 方式一：打开完整 Vivado 工程

命令：

```bash
make vivado-open
```

等价于打开 Vitis link 阶段保留下来的 Vivado 工程：

```text
build/hw/.../_x_temp/link/vivado/vpl/prj/prj.xpr
```

适合看：

- 工程结构和所有 generated IP。
- Vitis 生成的 block design / shell / kernel 接线。
- synthesis / implementation run 状态。
- 各阶段报告入口。
- 从工程 UI 里打开 implemented design。

打开后建议操作：

1. 左侧 `Flow Navigator` 里点 `Open Implemented Design`。
2. 左侧或上方切到 `Netlist`，展开层级。
3. 用搜索找自己的 kernel：`krnl_spmv`、`outer_product_mul_bits`、`outer_product_mul_dmul_ip`。
4. 对模块右键点 `Schematic`，看该层级的逻辑连接图。
5. 点 `Reports -> Report Utilization` 看资源。
6. 点 `Reports -> Report Timing Summary` 看时序。

Vivado Tcl Console 里也可以直接查层级：

```tcl
get_cells -hier -filter {NAME =~ *krnl_spmv*}
get_cells -hier -filter {NAME =~ *outer_product_mul*}
get_cells -hier -filter {NAME =~ *dmul_ip*}
```

`make vivado-open` 更适合“从 Vitis 工程结构往下钻”，比如看 HBM interconnect、AXI register slice、kernel IP 是怎么接进平台的。

## 方式二：直接打开 routed checkpoint

命令：

```bash
make vivado-routed
```

它会生成一个很小的 Tcl：

```text
build/open_routed_vivado.tcl
```

内容大致是：

```tcl
open_checkpoint {build/hw/.../level0_wrapper_routed.dcp}
```

然后用 Vivado GUI 打开最终布线后的 checkpoint。

适合看：

- routed 后的最终 netlist。
- 实际 placement / routing。
- `Device` 视图里的资源物理分布。
- SLR 使用情况。
- post-route timing。
- 模块在最终设计中的真实层级和位置。

打开后建议操作：

1. 打开 `Device` 视图，看 FPGA/SLR 上资源分布。
2. 在 `Netlist` 搜索 `krnl_spmv`，定位用户 kernel。
3. 选中 cell 后右键 `Highlight`，在 Device 视图里看位置。
4. 对 `outer_product_mul_bits` 或 `outer_product_mul_dmul_ip` 右键 `Schematic`，看 Chisel wrapper 和 FP64 IP 的连接。
5. 用 `Report Utilization` 时勾选 hierarchical，生成层级资源表。

常用 Tcl：

```tcl
report_utilization -hierarchical -file reports/hw/hier_util_routed.rpt
report_design_analysis -logic_level_distribution -file reports/hw/logic_level_distribution.rpt
report_timing_summary -file reports/hw/timing_summary_routed.rpt
```

如果只关心最终实现结果，`make vivado-routed` 通常比完整工程入口更直接。

## GUI 缩放

默认：

```bash
make vivado-open
```

使用：

```text
VIVADO_SCALE=2
```

也就是 200% 缩放。Makefile 会设置：

```text
QT_SCALE_FACTOR=2
_JAVA_OPTIONS=-Dsun.java2d.uiScale=2
```

如果界面太大或太小，可以覆盖：

```bash
make vivado-open VIVADO_SCALE=1
make vivado-open VIVADO_SCALE=1.5
make vivado-routed VIVADO_SCALE=2
```

注意：Vivado Linux GUI 的缩放来源比较复杂，可能同时受 Qt、Java、窗口管理器、X server DPI 影响。当前 Makefile 已经关闭 Qt 自动 HiDPI，并显式设置 Qt/Java scale。如果某个远程桌面环境仍然不听这个值，优先检查本地 X server 或远程桌面软件的 DPI 设置。

## X11 为什么慢

SSH X11 forwarding 下 Vivado 慢、从上到下刷新是正常现象。

原因是：

- Vivado GUI 很重，`Device` / `Schematic` / implemented design 会产生大量绘图操作。
- X11 forwarding 不是视频流，而是把大量 X 协议绘图请求通过 SSH 来回传。
- 网络延迟高时，每次窗口刷新都会被放大。
- routed design 的器件视图和 schematic 都包含大量对象，刷新成本很高。

缓解方式：

- 优先用 `make vivado-routed`，直接看最终 checkpoint，少加载工程上下文。
- 少开全局 `Device` 和巨大 schematic，先用 Tcl/Netlist 搜索定位模块，再局部打开。
- 能用报告解决的问题尽量用 `report_utilization` / `report_timing_summary`。
- 如果要长时间看 GUI，优先用 NoMachine、VNC、X2Go 这类远程桌面，而不是 SSH X11 forwarding。
- 远程桌面通常传压缩后的画面，比 X11 forwarding 对 Vivado 这类重 GUI 更友好。

## 下载到本地查看

可以把构建产物下载到本地 Vivado 里看，不需要本地有实体 U55C 板卡。

但注意：不建议只下载 `.xpr` 单个文件。Vivado 工程依赖旁边的 generated source、IP、run 目录和 checkpoint。更稳的做法是下载整个 Vitis link 阶段的 Vivado 目录：

```text
build/hw/xilinx_u55c_gen3x16_xdma_3_202210_1/_x_temp/link/vivado/vpl/
```

如果只想看最终 netlist、层级、Device 布局、resource 和 timing，最推荐下载 routed checkpoint：

```text
build/hw/xilinx_u55c_gen3x16_xdma_3_202210_1/_x_temp/link/vivado/vpl/prj/prj.runs/impl_1/level0_wrapper_routed.dcp
```

本地打开方式：

```bash
vivado level0_wrapper_routed.dcp
```

或者进入 Vivado 后：

```tcl
open_checkpoint level0_wrapper_routed.dcp
```

建议一起下载报告目录：

```text
reports/hw/xilinx_u55c_gen3x16_xdma_3_202210_1/
```

这样即使本地 Vivado 工程打开不完整，也能直接看 HLS、link、utilization、SLR、timing 报告。

推荐下载组合：

```text
build/hw/xilinx_u55c_gen3x16_xdma_3_202210_1/_x_temp/link/vivado/vpl/
reports/hw/xilinx_u55c_gen3x16_xdma_3_202210_1/
```

如果文件太大，可以最小化下载：

```text
level0_wrapper_routed.dcp
reports/hw/xilinx_u55c_gen3x16_xdma_3_202210_1/
```

## 通过 SSH 下载命令

服务器端不能无条件“一键下载到客户端”。正常 SSH 连接是客户端连服务器，服务器不知道客户端文件系统，也不能主动写入客户端磁盘。除非客户端也开 SSH 服务并允许服务器反连，否则正确方式是：

- 服务器端一键打包。
- 客户端执行脚本或 `scp/rsync` 拉取。

### 服务器端打包

在服务器的工程目录执行：

```bash
cd ~/ProjectFS/Project-X
make vivado-package
```

这会生成最小包：

```text
build/packages/project-x-vivado-view-min.tar.gz
```

内容包括 routed DCP、硬件报告和相关文档。

如果要完整 Vivado `vpl/` 工程目录：

```bash
make vivado-package-full
```

这会生成：

```text
build/packages/project-x-vivado-view-full.tar.gz
```

完整包更大，但更适合在本地看 Vitis/Vivado 工程结构、block design、IP 目录和实现 run。

### Linux/macOS 客户端脚本

在本地 Linux 或 macOS 终端执行：

```bash
./scripts/download-vivado-view.sh USER@SERVER ./project-x-vivado --min
```

下载完整工程：

```bash
./scripts/download-vivado-view.sh USER@SERVER ./project-x-vivado --full
```

只下载报告：

```bash
./scripts/download-vivado-view.sh USER@SERVER ./project-x-vivado --reports-only
```

如果服务器工程路径不是默认 `~/ProjectFS/Project-X`，可以这样覆盖：

```bash
PROJECT_X_REMOTE_ROOT=/path/to/Project-X ./scripts/download-vivado-view.sh USER@SERVER ./project-x-vivado --min
```

macOS 说明：AMD Vivado 2022.2 官方支持的是 Windows 和 Linux，不支持原生 macOS。因此 macOS 客户端适合下载、解压、查看文本报告；如果要打开 `.dcp/.xpr`，需要 Linux/Windows Vivado 环境，或者用虚拟机/远程桌面。

### Windows PowerShell 客户端脚本

Windows 10/11 通常自带 OpenSSH 客户端。PowerShell 里执行：

```powershell
.\scripts\download-vivado-view.ps1 USER@SERVER .\project-x-vivado -Mode min
```

下载完整工程：

```powershell
.\scripts\download-vivado-view.ps1 USER@SERVER .\project-x-vivado -Mode full
```

只下载报告：

```powershell
.\scripts\download-vivado-view.ps1 USER@SERVER .\project-x-vivado -Mode reports-only
```

如果服务器工程路径不同：

```powershell
.\scripts\download-vivado-view.ps1 USER@SERVER .\project-x-vivado -Mode min -RemoteRoot /path/to/Project-X
```

如果 PowerShell 禁止执行本地脚本，可以临时用：

```powershell
powershell -ExecutionPolicy Bypass -File .\scripts\download-vivado-view.ps1 USER@SERVER .\project-x-vivado -Mode min
```

### 手动 scp/rsync 命令

下面命令也是在本地机器上执行，不是在服务器 SSH shell 里执行。把 `USER@SERVER` 换成你的服务器登录名和地址。

只下载 routed checkpoint：

```bash
scp USER@SERVER:~/ProjectFS/Project-X/build/hw/xilinx_u55c_gen3x16_xdma_3_202210_1/_x_temp/link/vivado/vpl/prj/prj.runs/impl_1/level0_wrapper_routed.dcp .
```

下载报告目录：

```bash
scp -r USER@SERVER:~/ProjectFS/Project-X/reports/hw/xilinx_u55c_gen3x16_xdma_3_202210_1 ./project-x-hw-reports
```

下载完整 Vivado 工程目录：

```bash
scp -r USER@SERVER:~/ProjectFS/Project-X/build/hw/xilinx_u55c_gen3x16_xdma_3_202210_1/_x_temp/link/vivado/vpl ./project-x-vivado-vpl
```

大目录更推荐用 `rsync`，支持断点续传和进度显示：

```bash
rsync -avP USER@SERVER:~/ProjectFS/Project-X/build/hw/xilinx_u55c_gen3x16_xdma_3_202210_1/_x_temp/link/vivado/vpl ./project-x-vivado-vpl
rsync -avP USER@SERVER:~/ProjectFS/Project-X/reports/hw/xilinx_u55c_gen3x16_xdma_3_202210_1 ./project-x-hw-reports
```

如果你只想打一个压缩包下载，也可以在本地执行：

```bash
ssh USER@SERVER 'cd ~/ProjectFS/Project-X && tar -czf - build/hw/xilinx_u55c_gen3x16_xdma_3_202210_1/_x_temp/link/vivado/vpl reports/hw/xilinx_u55c_gen3x16_xdma_3_202210_1' > project-x-vivado-view.tar.gz
```

本地解压：

```bash
tar -xzf project-x-vivado-view.tar.gz
```

如果你已经在服务器 SSH shell 里，也可以先在服务器上打包：

```bash
cd ~/ProjectFS/Project-X
tar -czf project-x-vivado-view.tar.gz \
  build/hw/xilinx_u55c_gen3x16_xdma_3_202210_1/_x_temp/link/vivado/vpl \
  reports/hw/xilinx_u55c_gen3x16_xdma_3_202210_1
```

然后在本地机器执行：

```bash
scp USER@SERVER:~/ProjectFS/Project-X/project-x-vivado-view.tar.gz .
```

如果已经在服务器上执行了 `make vivado-package`，本地直接拉包即可：

```bash
scp USER@SERVER:~/ProjectFS/Project-X/build/packages/project-x-vivado-view-min.tar.gz .
scp USER@SERVER:~/ProjectFS/Project-X/build/packages/project-x-vivado-view-full.tar.gz .
```

本地限制：

- 本地需要安装 Vivado，最好和服务器版本一致，当前工程使用 Vivado 2022.2。
- 本地不需要 U55C 实体板卡。
- 本地最好有对应器件支持，U55C 对应器件是 `xcu55c-fsvh2892-2L-e`。
- 如果打开完整 `.xpr`，本地缺少 platform/IP cache 时可能出现 missing source 或 missing IP。
- 如果只是打开 routed `.dcp`，通常比 `.xpr` 更稳，因为很多最终实现信息已经固化在 checkpoint 里。

## 现有报告怎么看

不用开 Vivado GUI，也可以先看这些报告。

HLS 级资源和 latency：

```text
reports/hw/xilinx_u55c_gen3x16_xdma_3_202210_1/krnl_spmv/hls_reports/krnl_spmv_csynth.rpt
```

kernel 级 routed 资源：

```text
reports/hw/xilinx_u55c_gen3x16_xdma_3_202210_1/link/imp/impl_1_kernel_util_routed.rpt
```

全设计 routed 资源：

```text
reports/hw/xilinx_u55c_gen3x16_xdma_3_202210_1/link/imp/impl_1_full_util_routed.rpt
```

SLR 资源分布：

```text
reports/hw/xilinx_u55c_gen3x16_xdma_3_202210_1/link/imp/impl_1_slr_util_routed.rpt
```

post-route timing：

```text
reports/hw/xilinx_u55c_gen3x16_xdma_3_202210_1/link/imp/impl_1_hw_bb_locked_timing_summary_routed.rpt
```

## 建议查看顺序

第一次看设计时，建议按这个顺序：

1. 先看 `krnl_spmv_csynth.rpt`，确认 HLS 生成了哪些模块，特别是 `outer_product_mul_bits`。
2. 再看 `impl_1_kernel_util_routed.rpt`，确认 kernel 最终资源。
3. 用 `make vivado-routed` 打开 checkpoint，搜索 `krnl_spmv` 和 `outer_product_mul_bits`。
4. 只在需要理解平台连接时，再用 `make vivado-open` 打开完整工程。
