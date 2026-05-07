# Project-X：多 Black-Box 共存问题总结

这份文档总结 `Project-X` 在 `VARIANT=chisel_core` 下同时接入两个 Chisel/Vivado RTL black-box 时遇到的问题、定位过程和当前修法。

当前这里的两个 black-box 是：

```text
outer_product_mul_bits
spmv_row_muladd_bits
```

它们都在 HLS kernel `krnl_spmv` 里被调用，并且底层都依赖 Xilinx `floating_point` IP。

## 1. 现象

`hybrid` 版本可以正常通过硬件编译，但 `chisel_core` 在硬件编译时失败。

最开始看到的是 HLS compile 失败：

```text
Problem generating csynth RTL:
Found multiple blackbox RTL file name 'floating_point_v7_1_rfs.v' that was reused
```

解决掉这个错误以后，又遇到 VPL/Vivado synth 失败：

```text
module 'floating_point_v7_1_15' not found
```

触发位置在：

```text
spmv_row_dmul_ip.v
spmv_row_dadd_ip.v
```

## 2. 根因

### 2.1 HLS compile 阶段的冲突

两个 black-box JSON 都把同名文件列进了 `rtl_files`：

```text
floating_point_v7_1_rfs.v
```

Vitis HLS 会把这些文件视为同一工程内的 RTL 输入。  
当两个不同路径的同名文件同时出现时，HLS 会直接报冲突。

### 2.2 VPL synth 阶段的缺失

即使让 HLS compile 过了，`chisel_core` 导出的 `xo` 里还存在另一个问题：

- `xo` zip 包里已经有 `hdl/verilog/floating_point_v7_1_rfs.v`
- 但 `component.xml` 初始并没有把它完整登记为：
  - synthesis fileset 成员
  - simulation fileset 成员
  - `floating_point 7.1` 的 `subCoreRef copy_mode`
  - view 中的 ref fileset 引用

结果就是：

```text
xo 里有文件
Vivado 却没有把它当成 spmv_row_dmul_ip / spmv_row_dadd_ip 的静态依赖
```

于是到 VPL synth 时，`spmv_row_dmul_ip.v` 里实例化的：

```text
floating_point_v7_1_15
```

找不到定义。

## 3. 为什么 `hybrid` 没问题

`hybrid` 只有一套 black-box：

```text
outer_product_mul_bits
```

对应的 `xo` 导出结构里，`component.xml` 会自然带上：

- `hdl/verilog/floating_point_v7_1_rfs.v`
- `xilinx_verilogsynthesis_xilinx_com_ip_floating_point_7_1__ref_view_fileset`
- `xilinx_verilogbehavioralsimulation_xilinx_com_ip_floating_point_7_1__ref_view_fileset`
- `subCoreRef copy_mode`
- synthesis/simulation view 中的 ref fileset 引用

所以 `hybrid` 这一条链路本身是稳定的。

问题只在于：

```text
两个 black-box 同时共存时，
chisel_core 的 xo 导出没有把这套 floating_point 依赖完整保留下来。
```

## 4. 当前修法

当前修法分两层。

### 4.1 先让 HLS compile 不因同名 RTL 冲突而失败

处理原则：

- 不让两个 JSON 同时各自携带两份不同来源、同名的 `floating_point_v7_1_rfs.v`
- 保留能够让 HLS compile 顺利完成的一组 `rtl_files`

相关文件：

- [OuterProductMulFiles.scala](/home/pyx/ProjectFS/Project-X/hardware/chisel/src/main/scala/OuterProductMulFiles.scala)
- [SpmvRowMulAddFiles.scala](/home/pyx/ProjectFS/Project-X/hardware/chisel/src/main/scala/SpmvRowMulAddFiles.scala)
- [add_outer_product_blackbox.tcl](/home/pyx/ProjectFS/Project-X/hardware/chisel/add_outer_product_blackbox.tcl)
- [add_chisel_core_blackboxes.tcl](/home/pyx/ProjectFS/Project-X/hardware/chisel/add_chisel_core_blackboxes.tcl)

### 4.2 在 `xo` 生成后补齐 `component.xml`

这是当前真正让两套 black-box 共存的关键补丁。

新增脚本：

- [patch_xo_floating_point.py](/home/pyx/ProjectFS/Project-X/scripts/patch_xo_floating_point.py)

它会在 `chisel_core` 的 `xo` 生成后自动做这些事：

1. 解包 `krnl_spmv.xo`
2. 找到 `ip_repo/.../component.xml`
3. 检查 `hdl/verilog/floating_point_v7_1_rfs.v` 是否已经在包里
4. 如果在，就补齐：
   - synthesis fileset 中的 `floating_point_v7_1_rfs.v`
   - simulation fileset 中的 `floating_point_v7_1_rfs.v`
   - `xilinx_com_ip_floating_point_7_1__ref_view_fileset`
   - `subCoreRef copy_mode`
   - synthesis / simulation view 中的 ref fileset 引用
5. 重新打包 `xo`

Makefile 中，当前只对 `VARIANT=chisel_core` 自动启用这个后处理：

- [Makefile](/home/pyx/ProjectFS/Project-X/Makefile)

## 5. 当前验证结果

已经验证到以下结论：

1. `make xo TARGET=hw VARIANT=chisel_core` 可以通过。
2. 补丁前，`component.xml` 缺少 `floating_point` 静态依赖条目。
3. 补丁后，`component.xml` 已经补齐：
   - `floating_point_v7_1_rfs.v`
   - `subCoreRef copy_mode`
   - view 中的 ref fileset 引用
4. 重新跑 `make build-hw-chisel-core` 后，之前的错误：

```text
module 'floating_point_v7_1_15' not found
```

已经不再在 `vpl synth` 开始阶段立刻出现。

更具体地说，重新构建时已经成功进入：

```text
Block-level synthesis in progress
Top-level synthesis in progress
Run vpl: Step synth: Completed
Run vpl: Step impl: Started
```

这说明“两个 black-box 共存导致的浮点依赖缺失”这个核心坎已经被跨过去了。

## 6. 这份修法的定位

这不是一个抽象理论方案，而是针对当前 Vitis/Vivado 2022.2 工具行为的工程化 workaround。

它的特点是：

- 不改变现有两个 black-box 的上层接口
- 不要求立刻重写 `spmv_row` 的底层实现
- 先把 `xo` 导出元数据补全，让现有设计能继续往后走

如果后续还要扩展到更多 Chisel black-box，可以继续沿着这个方向做：

```text
先检查 xo 里有没有带齐静态依赖文件
再检查 component.xml / views / ref fileset / subCoreRef 是否完整
必要时做 xo 后处理
```

## 7. 相关命令

当前和这个问题最相关的命令有：

```bash
make chisel
make xo TARGET=hw VARIANT=chisel_core
make build-hw-chisel-core
```

如果需要单独运行补丁脚本：

```bash
python3 scripts/patch_xo_floating_point.py build/chisel_core/hw/xilinx_u55c_gen3x16_xdma_3_202210_1/krnl_spmv.xo
```

## 8. 后续建议

如果以后再增加第三个、第四个 Chisel black-box，建议优先检查：

1. JSON 的 `rtl_files` 是否重复引入同名静态 RTL
2. `xo` 包里是否真的带出了对应文件
3. `component.xml` 是否给这些文件建立了完整的：
   - fileSet
   - ref fileset
   - subCoreRef
   - view 引用

只要这四层有一层缺失，就可能再次出现：

```text
HLS compile 过了
但 VPL/Vivado synth 报 module not found
```
