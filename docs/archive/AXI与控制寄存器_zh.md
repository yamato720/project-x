# Project-X：AXI、AXI-Lite 与控制寄存器详解

这份文档是专门为下面这个困惑写的：

```text
“我以前接触的 AXI，感觉就是地址、数据、ready/valid 握手，最多再带几个保护位；
这里为什么突然冒出来一个‘控制寄存器’？”
```

这个困惑非常正常，因为你其实碰到了 **两个不同层次的视角**：

1. **总线事务视角**：AXI 上有哪些通道、地址怎么发、握手怎么走
2. **外设/加速器视角**：总线另一端挂的这个模块，内部到底暴露了什么可读写状态

你以前熟悉的，多半是第 1 层。  
而 `Project-X` 里的 `s_axilite` / `m_axi`，是在第 1 层之上，又把第 2 层也显式摆出来了。

---

## 1. 一句话先把误会拆开

先记住这句最关键的话：

```text
控制寄存器不是“AXI 额外多出来的一根神秘信号”。
控制寄存器是：挂在 AXI-Lite slave 后面、通过地址访问的一组模块内部寄存器。
```

也就是说：

```text
AXI/AXI-Lite 负责“怎么传”
控制寄存器负责“传到了以后，模块内部把值存在哪、拿来干什么”
```

所以这两件事不是冲突关系，而是上下层关系。

---

## 2. 先把 AXI 家族分开

在这个工程里，最相关的是三类：

### 2.1 AXI4 Memory-Mapped

这是完整的 memory-mapped AXI，总线两端通过地址访问内存或外设。

你会看到：

```text
AWADDR / WDATA / BRESP
ARADDR / RDATA / RRESP
AWVALID / AWREADY
WVALID  / WREADY
ARVALID / ARREADY
RVALID  / RREADY
```

它支持：

```text
地址
数据
读写响应
burst
多 beat 传输
```

在 `Project-X` 里，`m_axi` 对应的就是这类接口。

### 2.2 AXI4-Lite

这是简化版的 memory-mapped AXI。

你仍然会看到：

```text
AWADDR / WDATA / ARADDR / RDATA / ready/valid
```

但它通常用于：

```text
寄存器访问
配置
状态读取
启动/停止控制
```

它不追求高吞吐大块搬运，而是追求：

```text
简单、可寻址、易于做控制面
```

在 `Project-X` 里，`s_axilite` 对应的就是这类接口。

### 2.3 AXI4-Stream

这是另一套思路，没有地址概念，靠 `TVALID/TREADY` 推流。

如果你以前做的是纯流式接口，就更容易觉得：

```text
“AXI 不就是握手和数据吗，哪来的控制寄存器？”
```

因为 stream 本来就不是 memory-mapped 那一类。

当前 `Project-X` 没有用 AXI-Stream，但把它单独拿出来很有帮助，因为很多认知混淆都来自这里。

---

## 3. 你以前看到的“地址 + 握手”，其实还是总线层

假设你看到一个 AXI-Lite 写事务，大致像这样：

```text
master 发 AWADDR = 0x10
master 发 WDATA  = 8
slave 通过 AWREADY/WREADY 接收
slave 返回 BRESP
```

如果只看总线层，你看到的确实只是：

```text
地址
数据
握手
响应
```

但 **slave 内部** 还必须做一件事：

```text
把地址 0x10 解码成“某个寄存器”
把 WDATA 写进那个寄存器
```

这一步如果不做，这条 AXI 写事务就只是“收到了一个包”，没有任何电路语义。

所以所谓“控制寄存器”，其实就是：

```text
AXI-Lite slave 内部被地址映射的一组寄存器
```

---

## 4. 控制寄存器到底是什么

最朴素地说，它就是模块内部一组普通寄存器，例如：

```verilog
reg [31:0] num_rows_reg;
reg [63:0] scale_reg;
reg [63:0] col_idx_base_reg;
reg        ap_start_reg;
reg        ap_done_reg;
```

区别只在于：

```text
这些寄存器不是“随便内部自己改”，
而是通过 AXI-Lite 地址访问被 host 读写。
```

于是你可以给它们安排地址映射，例如：

```text
0x00  control/status
0x10  num_rows
0x18  scale low
0x1c  scale high
0x20  col_idx base low
0x24  col_idx base high
...
```

这时所谓“写控制寄存器”，翻成总线语言就是：

```text
master 对某个 AXI-Lite 地址发起写事务
slave 解码地址
把数据写进对应寄存器
```

所以“控制寄存器”不是另外一套协议；  
它是 **memory-mapped 总线背后的寄存器组织方式**。

---

## 5. 为什么 HLS/XRT 语境里总强调控制寄存器

因为对 FPGA kernel 而言，host 真正需要做的通常就是两类事：

1. 传几个标量参数
2. 告诉 kernel：输入/输出 buffer 在 device memory 的哪个地址

这两类信息都很适合通过 AXI-Lite 寄存器传。

所以 HLS 会自动帮你生成这样一块控制平面：

```text
标量参数寄存器
地址寄存器
启动/完成状态位
```

XRT 再把它包装成你在 C++ 里看到的：

```cpp
auto run = kernel(rows, scale, col_idx_bo, values_bo, x_bo, y_bo);
```

你没手写 AXI 事务，不代表这些事务不存在。  
只是 XRT 替你把“写寄存器、拉 start、轮询 done”做掉了。

---

## 6. 在 Project-X 里，`s_axilite` 和 `m_axi` 到底怎么分工

对应文件：

```text
~/ProjectFS/Project-X/hardware/krnl_spmv.cpp
```

### 6.1 `s_axilite`

这些 pragma：

```cpp
#pragma HLS INTERFACE s_axilite port = num_rows bundle = control
#pragma HLS INTERFACE s_axilite port = scale bundle = control
#pragma HLS INTERFACE s_axilite port = col_idx bundle = control
#pragma HLS INTERFACE s_axilite port = values bundle = control
#pragma HLS INTERFACE s_axilite port = x bundle = control
#pragma HLS INTERFACE s_axilite port = y bundle = control
#pragma HLS INTERFACE s_axilite port = return bundle = control
```

HLS 看到后，会生成：

```text
一个 AXI-Lite slave 接口
+ 一组和参数一一对应的寄存器
```

在当前工程里，这组寄存器大致承载：

```text
num_rows
scale
col_idx 的 base address
values 的 base address
x 的 base address
y 的 base address
ap_start / ap_done / ap_idle / ap_ready
```

### 6.2 `m_axi`

这些 pragma：

```cpp
#pragma HLS INTERFACE m_axi port = col_idx offset = slave bundle = gmem_col
#pragma HLS INTERFACE m_axi port = values offset = slave bundle = gmem_val
#pragma HLS INTERFACE m_axi port = x offset = slave bundle = gmem_x
#pragma HLS INTERFACE m_axi port = y offset = slave bundle = gmem_y
```

HLS 看到后，会生成：

```text
4 套 AXI4 master memory interface
```

它们干的是：

```text
真正去外部内存读写大块数组数据
```

所以当前 `Project-X` 的分工可以浓缩成这两句：

```text
s_axilite 负责“参数和地址告诉 kernel”
m_axi     负责“kernel 真正去外存把数据搬回来/写出去”
```

---

## 7. `offset = slave` 为什么把这两层连起来了

这条最值得盯住：

```cpp
#pragma HLS INTERFACE m_axi port = x offset = slave bundle = gmem_x
```

这里的 `offset = slave` 基本可以理解成：

```text
这个 m_axi 端口访问外部内存时，需要一个 base address；
这个 base address 不是写死的，而是从 AXI-Lite control 寄存器里来。
```

所以完整链路是：

```text
host.cpp 里创建 x_bo
  -> XRT 知道 x_bo 在 device memory 的地址
  -> XRT 把这个地址写进 control 寄存器
  -> kernel 内部的 gmem_x AXI master 拿这个地址当 base
  -> 再用 x[col] 的偏移去发起 AXI 读请求
```

这也是为什么在这个工程里：

```text
控制寄存器不是“可有可无的小配角”
而是 m_axi 访存真正起点的一部分
```

---

## 8. 为什么你以前没明显感受到“控制寄存器”

常见有这几种情况：

### 8.1 你以前看的只是总线时序图

那你自然只会看到：

```text
地址
数据
握手
响应
```

不会看到“地址背后 actually 对应哪个寄存器”。

### 8.2 你以前自己写的 slave 很简单

可能你内部就几个 `case (addr)`，但你没把它明确命名成“寄存器映射”。

例如：

```verilog
case (awaddr)
  32'h10: num_rows_reg <= wdata;
  32'h14: start_reg    <= wdata[0];
endcase
```

这其实已经是：

```text
AXI-Lite 控制寄存器
```

只是当时你把它当成“地址解码 + 若干 reg”来看了，没有用这个术语。

### 8.3 你以前更多做的是 stream

如果主要是 AXI-Stream，那么确实没有“按地址访问一组寄存器”这件事。

那你看到 `s_axilite` 的时候就会觉得画风完全不同。

---

## 9. `PROT` 这些位和控制寄存器是什么关系

你提到“最多加个保护位”，这里正好可以拆开说。

像 `AWPROT` / `ARPROT` 这样的位，属于：

```text
AXI 协议字段
```

它们描述的是事务属性，例如：

```text
privileged / secure / instruction/data access 之类
```

而控制寄存器属于：

```text
slave 模块内部的 memory-mapped 状态
```

所以它们根本不是一个层次的东西：

```text
PROT 是“这笔事务带了什么属性”
控制寄存器是“这笔事务最终写进模块内部哪一个寄存器”
```

在很多简单设计里，`PROT` 可能几乎不被认真使用，甚至被忽略；  
但寄存器映射通常依然存在，因为模块总得知道 host 想改哪个参数。

---

## 10. 在 U55C + XRT 这套栈里，控制寄存器为什么尤其常见

因为这套栈本来就分成：

```text
control plane
data plane
```

对当前工程来说：

### control plane

走的是：

```text
host/XRT -> AXI-Lite control registers
```

承载：

```text
rows
scale
各 buffer 的 base address
start/done
```

### data plane

走的是：

```text
kernel m_axi -> platform interconnect -> U55C HBM
```

承载：

```text
col_idx[]
values[]
x[]
y[]
```

这就是为什么文档里一直会把“控制寄存器”单独说出来。  
不是它比 AXI 更底层，而是它正好是 control plane 的核心抽象。

---

## 11. 用当前工程走一遍完整例子

看这句：

```cpp
auto run = kernel(rows, scale, col_idx_bo, values_bo, x_bo, y_bo);
```

你可以在脑子里把它展开成近似下面这串动作：

```text
1. XRT 取出 rows 和 scale
2. XRT 取出 col_idx_bo / values_bo / x_bo / y_bo 的 device address
3. XRT 通过 AXI-Lite 写 control 寄存器：
   - num_rows 寄存器
   - scale 寄存器
   - col_idx base address 寄存器
   - values base address 寄存器
   - x base address 寄存器
   - y base address 寄存器
4. XRT 置位 ap_start
5. kernel 开始跑
6. kernel 里的 gmem_col/gmem_val/gmem_x/gmem_y 四个 AXI master
   按这些 base address 去外部内存发起访问
7. kernel 跑完后置位 ap_done
8. XRT/host 等待完成，再把输出 BO sync 回 host
```

如果这样看，你就会发现：

```text
“控制寄存器”在这条链里一点都不奇怪，
它只是 host 告诉 kernel ‘参数是什么、数据在哪’ 的标准入口。
```

---

## 12. 一个非常实用的判断法

以后你看到一个接口，如果你想判断它更像哪类 AXI，可以直接问：

### 问题 1：它主要搬的是大块数组，还是几个参数？

如果是：

```text
几个参数 / 状态位 / 启停控制
```

那它大概率属于：

```text
AXI-Lite + 控制寄存器
```

如果是：

```text
大块内存数据
```

那它大概率属于：

```text
AXI4 memory-mapped master/slave
```

### 问题 2：访问靠不靠地址？

如果：

```text
按地址访问某个 offset
```

那多半是 memory-mapped，通常就会自然出现“寄存器映射”这个概念。

如果：

```text
没有地址，只是数据流向前推
```

那更可能是 stream。

---

## 13. 把这件事压成一句最终心智模型

对当前 `Project-X`，最推荐你记下面这句：

```text
AXI-Lite control 寄存器 = host 给 kernel 下命令、传标量、传 buffer 基地址的入口
AXI4 m_axi             = kernel 真正去 HBM 读写数组数据的通道
```

如果把这句记住，`s_axilite` 和 `m_axi` 基本就不会再混。

---

## 14. 建议和哪几份文档配合看

如果你现在想继续顺着这条线走，推荐这样看：

1. 先看这份，建立“AXI 总线层”和“控制寄存器层”之间的关系
2. 再看 [HLS pragma到U55C硬件对照_zh.md](<~/ProjectFS/Project-X/docs/HLS pragma到U55C硬件对照_zh.md:1>)，把 `s_axilite/m_axi` 和 pragma 对上
3. 再看 [BO与HBM映射_zh.md](~/ProjectFS/Project-X/docs/BO与HBM映射_zh.md:1)，把 base address / BO / HBM 串起来
4. 最后回到 [krnl_spmv.cpp](~/ProjectFS/Project-X/hardware/krnl_spmv.cpp:16) 和 [host.cpp](~/ProjectFS/Project-X/software/host.cpp:156) 对照读

这样会比较顺。
