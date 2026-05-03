# Chisel FP64 IP 接入 HLS

这份文档说明当前 `Project-X` 如何把 Chisel 生成的模块接进 Vitis HLS kernel，并把外积阶段的乘法换成 Xilinx Floating Point FP64 乘法 IP。

## 目标

当前 HLS kernel 做两件事：

```text
y[row] = scale * A[row, :] * x
yy_t[row, col] = y[row] * y[col]
```

`yy_t` 的每个元素都需要一次 FP64 乘法。原来这一句直接写在 C++ 里：

```cpp
yy_t[row * num_rows + col] = lhs * y_buffer[col];
```

现在改成：

```cpp
outer_product_mul_bits(projectx_double_to_bits(lhs),
                       projectx_double_to_bits(y_buffer[col]),
                       product_bits);
yy_t[row * num_rows + col] = projectx_bits_to_double(product_bits);
```

也就是说，HLS 看到的是一个外部函数 `outer_product_mul_bits(...)`。综合时这个函数由 RTL black-box 实现；软件仿真时用 C++ fallback/model 保持数值一致。

## 文件分工

关键文件如下：

```text
hardware/krnl_spmv.cpp
hardware/chisel/src/main/scala/OuterProductMul.scala
hardware/chisel/src/main/scala/OuterProductMulFiles.scala
hardware/chisel/src/main/scala/Generate.scala
hardware/chisel/add_outer_product_blackbox.tcl
Makefile
```

建议按这个顺序读源码注释：

```text
1. hardware/krnl_spmv.cpp
   看 HLS kernel 如何把外积乘法写成 outer_product_mul_bits(...) 函数调用。

2. hardware/chisel/add_outer_product_blackbox.tcl
   看 v++ 如何在 HLS 综合前执行 add_files -blackbox。

3. hardware/chisel/src/main/scala/OuterProductMul.scala
   看 Chisel wrapper 如何暴露 HLS ap_ctrl_chain 端口，并实例化 Vivado FP64 IP。

4. hardware/chisel/src/main/scala/OuterProductMulFiles.scala
   看 .hpp / C model / black-box JSON / Vivado IP Tcl 是怎么一起生成的。

5. Makefile
   看 make xo 前如何保证 Chisel wrapper、black-box JSON、Vivado IP RTL 都已存在。
```

`OuterProductMul.scala` 负责生成 `outer_product_mul_bits.v`。这个 Verilog wrapper 暴露 HLS black-box 需要的端口：

```text
ap_clk
ap_rst
ap_ce
ap_start
ap_continue
lhs_bits
rhs_bits
ap_idle
ap_done
ap_ready
result_bits_ap_vld
result_bits
```

wrapper 内部实例化 Vivado IP：

```text
outer_product_mul_dmul_ip
```

这个 IP 是 Xilinx `floating_point` IP，配置为：

```text
operation_type = Multiply
precision      = Double
latency        = 6
flow_control   = NonBlocking
aclken         = true
```

`OuterProductMulFiles.scala` 负责额外生成三类文件：

```text
hardware/chisel/generated/outer/outer_product_mul.hpp
hardware/chisel/generated/outer/outer_product_mul_model.cpp
hardware/chisel/generated/outer/outer_product_mul.json
hardware/chisel/generated/outer/create_outer_product_mul_dmul_ip.tcl
```

其中 `outer_product_mul.json` 是 Vitis HLS black-box 描述文件，告诉 HLS：

```text
C 函数名是什么
RTL 顶层模块名是什么
C 参数对应哪些 RTL 端口
clock/reset/start/done/ready 端口叫什么
RTL 文件有哪些
C model 文件在哪
```

`create_outer_product_mul_dmul_ip.tcl` 会用 Vivado 生成 Floating Point IP 的综合/仿真 RTL：

```text
hardware/chisel/generated/outer/ip/outer_product_mul_dmul_ip/synth/outer_product_mul_dmul_ip.v
hardware/chisel/generated/outer/ip/outer_product_mul_dmul_ip/hdl/floating_point_v7_1_rfs.v
```

## 构建链路

`Makefile` 里把 Chisel 和 Vivado IP 生成接到了 `xo` 前面：

```make
$(XO): $(KERNEL_SRC) \
       $(CHISEL_OUTER_V) \
       $(CHISEL_OUTER_HPP) \
       $(CHISEL_OUTER_MODEL) \
       $(CHISEL_OUTER_JSON) \
       $(CHISEL_OUTER_IP_V) \
       $(CHISEL_OUTER_IP_RFS) \
       $(HLS_PRE_TCL)
```

执行：

```bash
make run-sw ROWS=8 SCALE=2 X0=1
```

实际顺序是：

```text
1. sbt runMain projectx.GenerateAll
2. 生成 Chisel wrapper 和 black-box companion 文件
3. vivado -mode batch -source create_outer_product_mul_dmul_ip.tcl
4. 生成 Xilinx Floating Point FP64 multiply IP RTL
5. v++ -c 编译 HLS kernel 为 xo
6. HLS pre_tcl 执行 add_files -blackbox outer_product_mul.json
7. v++ -l 生成 sw_emu xclbin
8. host.exe 加载 xclbin 并校验 y / yy_t
```

## HLS 为什么需要 pre_tcl

`v++` 自动生成的 HLS Tcl 默认只会加入：

```tcl
add_files hardware/krnl_spmv.cpp
```

black-box 文件不是普通 C++ include，必须额外执行：

```tcl
add_files -blackbox hardware/chisel/generated/outer/outer_product_mul.json
```

所以工程里有：

```text
hardware/chisel/add_outer_product_blackbox.tcl
```

并且 `Makefile` 传给 `v++`：

```make
--hls.pre_tcl $(HLS_PRE_TCL)
```

这样 HLS 在 `csynth_design` 前就能知道 `outer_product_mul_bits(...)` 不是普通未定义函数，而是 RTL black-box。

## 软件仿真为什么还要 C++ fallback

`sw_emu` 运行时会把 kernel 编成 CPU 侧动态库。这个阶段不一定会把 black-box JSON 里的 C model 链进最终运行库。

因此 `hardware/krnl_spmv.cpp` 里保留了：

```cpp
#ifndef __SYNTHESIS__
void outer_product_mul_bits(...) {
    ...
}
#endif
```

这段只用于非综合路径，解决 `sw_emu` 动态库符号缺失问题。综合时 `__SYNTHESIS__` 生效，HLS 不会使用这个 fallback，而是用 `outer_product_mul.json` 指向的 RTL black-box。

## 数据类型为什么走 bits

Vitis HLS black-box 对 `double` 端口支持不如整数位宽端口直接。当前做法是：

```text
double -> unsigned long long bits -> RTL 64-bit port -> unsigned long long bits -> double
```

RTL 侧的 `outer_product_mul_dmul_ip` 把这 64 bit 解释为 IEEE-754 double 输入，输出同样是 IEEE-754 double bit pattern。

## 当前验证状态

当前这条链路已经通过硬件编译/打包，工作区里已有硬件产物：

```text
build/hw/xilinx_u55c_gen3x16_xdma_3_202210_1/krnl_spmv.xclbin
```

也就是说，`outer_product_mul_bits` 不是只在 `sw_emu` 里靠 C++ fallback 跑通；硬件 `v++ -c` / `v++ -l` 也已经接受了这个 RTL black-box，并把相关 RTL 打进 kernel IP repo。

目前可以确认“用上了 IP 核”的证据有三层：

1. Chisel 生成的 wrapper 里实例化了 Vivado IP wrapper：

```verilog
outer_product_mul_dmul_ip dmul (...);
```

对应文件：

```text
hardware/chisel/generated/outer/outer_product_mul_bits.v
```

2. HLS black-box JSON 把 wrapper、IP wrapper 和 Floating Point IP RTL 都列进了 `rtl_files`：

```text
outer_product_mul_bits.v
ip/outer_product_mul_dmul_ip/synth/outer_product_mul_dmul_ip.v
ip/outer_product_mul_dmul_ip/hdl/floating_point_v7_1_rfs.v
```

对应文件：

```text
hardware/chisel/generated/outer/outer_product_mul.json
```

3. Vivado 生成的 IP wrapper 明确标识为 Xilinx Floating Point IP：

```text
IP VLNV: xilinx.com:ip:floating_point:7.1
X_CORE_INFO = floating_point_v7_1_15,Vivado 2022.2
```

对应文件：

```text
hardware/chisel/generated/outer/ip/outer_product_mul_dmul_ip/synth/outer_product_mul_dmul_ip.v
```

另外，硬件报告里也能看到 HLS 把该 black-box 当成硬件模块实例：

```text
grp_outer_product_mul_bits_fu_297 | outer_product_mul_bits | latency 6 | II 1 | yes(flp)
```

对应报告：

```text
reports/hw/xilinx_u55c_gen3x16_xdma_3_202210_1/krnl_spmv/hls_reports/krnl_spmv_csynth.rpt
```

因此结论是：当前硬件编译路径确实走到了 `Chisel wrapper -> outer_product_mul_dmul_ip -> Xilinx floating_point FP64 multiply IP` 这条链路。

需要注意的是，这里说的“硬件通过”指硬件编译/链接/打包通过。如果要证明板卡实际运行结果，还需要用已有硬件 xclbin 跑 host：

```bash
make run-hw-existing ROWS=8 SCALE=2 X0=1 TIMING=1 WARMUP=1 REPEAT=5
```

host 输出 `PASS` 后，才能说明板上执行结果也和 CPU golden 对齐。

## 当前限制

硬件综合有一个关键约束：当前 black-box 使用 `ap_ctrl_chain` 协议，Vitis HLS 不允许 `ap_ctrl_chain` black-box 出现在 `#pragma HLS PIPELINE` 的循环区域里。因此 `OuterProductColLoop` 不能直接写：

```cpp
#pragma HLS PIPELINE II=1
outer_product_mul_bits(...);
```

否则硬件编译会报：

```text
Ap_ctrl_chain blackbox module 'outer_product_mul_bits' can not be used in pipeline region.
```

当前修复是在外积列循环上显式关闭 pipeline，并禁止外积两层循环 flatten：

```cpp
OuterProductRowLoop:
for (...) {
#pragma HLS LOOP_FLATTEN off

OuterProductColLoop:
    for (...) {
#pragma HLS PIPELINE off
        outer_product_mul_bits(...);
    }
}
```

只删除 `PIPELINE II=1` 不够，因为 Vitis HLS 可能会自动 pipeline 内层循环，然后仍然触发同一个错误。显式 `PIPELINE off` 才是这里的关键。这个修复能优先保证硬件综合链路继续往下走，代价是外积阶段吞吐会低于原来期望的 II=1。

后续如果调整 IP 配置，或者板卡实测结果不对，优先检查：

```text
HLS black-box latency 是否和 IP 实际 latency 一致
ap_ready/ap_done 是否满足 HLS 调度期望
FP IP 资源和时序是否满足 U55C 目标频率
link 后是否正确打包所有 IP RTL
```

当前 IP latency 配成 6，JSON 里也写 `latency = 6`。如果后续改 Vivado IP latency，这两个地方必须保持一致。

如果后续要恢复外积阶段 II=1，更合理的方向不是把 `ap_ctrl_chain` black-box 塞回 pipeline loop，而是把乘法单元改成纯组合/无控制接口可 pipeline 的 black-box，或者把整个外积阶段做成独立 RTL/Chisel kernel，用 AXI master/stream 接口自己处理读写和流水。
