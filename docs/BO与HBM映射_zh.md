# Project-X：BO 与 HBM 映射说明

这份文档专门解释下面这几个容易混淆的问题：

1. `BO` 到底是什么
2. `HBM[0] ~ HBM[3]` 到底表示什么
3. kernel 里的指针和 host 指针是不是一回事
4. 为什么这里没有手工指定“起始地址”
5. 一个 HBM bank 里能不能放多个数组

---

## 1. 先看结论

对当前 `Project-X` demo，可以先记这几句话：

1. `BO` 是 XRT 分配的 **设备内存对象**，不是普通 C++ 裸指针。
2. `HBM[0] ~ HBM[3]` 表示 U55C 上不同的 HBM memory bank。
3. `col_idx / values / x / y` 放进去的是 **数组内容**，不是 host 指针值。
4. 当前 demo 不需要你手工指定起始地址，XRT 会在选定 bank 内自动分配一段地址空间。
5. 一个 bank 里当然可以放多个数组，只要容量够；只是会共享这个 bank 的带宽。

---

## 2. BO 是什么

在当前代码里，最关键的是这段：

```cpp
xrt::bo bo(device, data.size() * sizeof(T), kernel.group_id(arg_index));
```

文件位置：

```text
/home/pyx/ProjectFS/Project-X/software/host.cpp
```

它的意思不是“创建一个普通指针”，而是：

```text
在 FPGA 设备上分配一块内存
并返回一个由 XRT 管理的 buffer object 句柄
```

所以 `BO = Buffer Object`，你可以把它理解成：

- 一块设备侧内存
- 一个 host 侧可操作的对象
- 自带 map / sync 等接口

### 2.1 这个 BO 后面做了什么

当前流程是：

```cpp
auto mapped = bo.map<T*>();
std::copy(data.begin(), data.end(), mapped);
bo.sync(XCL_BO_SYNC_BO_TO_DEVICE, ...);
```

含义是：

1. 先把 BO 映射到 host 可访问的地址
2. 把 `std::vector<T>` 里的内容拷进去
3. 再同步到 FPGA 设备侧

所以真正进 HBM 的不是指针，而是：

```text
vector 里的实际数组元素
```

---

## 3. `HBM[0] ~ HBM[3]` 是什么

当前配置文件是：

```ini
[connectivity]
sp=krnl_spmv_1.col_idx:HBM[0]
sp=krnl_spmv_1.values:HBM[1]
sp=krnl_spmv_1.x:HBM[2]
sp=krnl_spmv_1.y:HBM[3]
```

文件位置：

```text
/home/pyx/ProjectFS/Project-X/cfg/u55c.cfg
```

这里的意思是：

- `col_idx` 这个 kernel memory port 接到 `HBM[0]`
- `values` 接到 `HBM[1]`
- `x` 接到 `HBM[2]`
- `y` 接到 `HBM[3]`

所以你文档里写成：

```text
HBM[0] <- col_idx
HBM[1] <- values
HBM[2] <- x
HBM[3] <- y
```

从教学理解上是对的。

但更精确一点说，不是“整个 bank 只属于这一个数组”，而是：

```text
这个数组对应的 BO 被分配到这个 bank 的地址空间里
kernel 通过这个 bank 对应的 AXI 端口去访问它
```

---

## 4. 放进去的是“指针”还是“数组内容”

答案是：

```text
放进去的是数组内容，不是 host 指针值
```

例如：

```cpp
auto col_idx_bo = make_bo(device, kernel, 2, col_idx);
```

这里 `col_idx` 是一个 `std::vector<int>`。  
XRT 做的是：

1. 在设备上分配一块 BO
2. 把 `col_idx[0], col_idx[1], ...` 这些整数数据拷进去
3. 同步到对应的 device memory bank

所以进入 `HBM[0]` 的是：

```text
col_idx 数组本体
```

不是：

```text
host 进程里的指针地址
```

同理：

- `HBM[1]` 里放的是 `values[]`
- `HBM[2]` 里放的是 `x[]`
- `HBM[3]` 里放的是 `y[]`

---

## 5. kernel 里的 `const int* col_idx` 是什么

在 C 层你看到的是：

```cpp
const int* col_idx
```

但在硬件语义里，它不等于 CPU 程序里的普通虚拟地址指针。

更接近这样理解：

```text
一个 AXI memory port
+ 一个 base address 寄存器
+ HLS 生成的地址访问逻辑
```

所以 kernel 里写：

```cpp
const int col = col_idx[idx];
```

在硬件里真实发生的是：

1. 从控制寄存器里得到 `col_idx` 对应的 base address
2. 用 `idx` 算出偏移
3. 通过 `m_axi_gmem_col` 发起一次 AXI 读请求
4. 从接到的 HBM bank 里把这个元素取回来

所以它更像：

```text
“设备内存中的一段数组的起始地址”
```

而不是 host 那边 `malloc/new` 出来的普通指针。

---

## 6. 为什么这里没有手工指定“起始位置”

这是一个很好的问题。

当前 demo 里你只指定了：

```text
这个 BO 应该放到哪个 memory group / bank
```

并没有指定：

```text
它必须从 bank 内部的哪个绝对偏移地址开始
```

原因是：

```text
默认情况下，这件事由 XRT 的设备内存分配器自动完成
```

也就是说，当前这句：

```cpp
xrt::bo(device, size, kernel.group_id(arg_index))
```

已经告诉 XRT：

1. 分配 `size` 这么大的一块设备内存
2. 放到 `group_id(arg_index)` 对应的那个 bank

至于：

- 这块内存在 bank 内部从哪一段地址开始
- 和别的 BO 怎么错开
- 对齐怎么处理

这些都由 runtime 负责。

所以当前这个教学工程里：

```text
不需要手工指定起始地址
```

### 6.1 那起始地址完全不存在吗

不是。它是存在的，只是：

- 由 XRT 自动分配
- host 通常不需要手写这个数值
- XRT 会把对应的设备地址写进 kernel 控制寄存器

所以不是“没有起始地址”，而是：

```text
起始地址存在，但默认由 runtime 隐式分配和传递
```

---

## 7. 一个 bank 里能不能塞多个数组

可以，完全可以。

一个 HBM bank 本质上就是一片设备内存地址空间。  
只要容量够，你可以在同一个 bank 里放：

- 一个 BO
- 两个 BO
- 很多个 BO

XRT 会给它们分配 **互不重叠** 的地址范围。

所以答案是：

```text
可以，一个 bank 里当然能放多个数组
```

### 7.1 那为什么当前 demo 分成 4 个 bank

不是因为“一个 bank 只能放一个数组”，而是因为：

```text
希望不同数组尽量走不同 memory port / bank，减少带宽争用
```

当前拆法：

- `col_idx` 一个 bank
- `values` 一个 bank
- `x` 一个 bank
- `y` 一个 bank

这是在模仿真实 FPGA/HBM 设计里常见的思路：

```text
把访问模式不同、带宽需求不同的数据拆到不同 bank
```

这样 kernel 在同一时刻访问这些数据时，冲突会更少。

---

## 8. 如果把多个数组塞到同一个 bank，会怎样

从“能不能跑”看，通常是可以的。  
从“性能会不会受影响”看，就未必好了。

比如你完全可以写成：

```ini
sp=krnl_spmv_1.col_idx:HBM[0]
sp=krnl_spmv_1.values:HBM[0]
sp=krnl_spmv_1.x:HBM[0]
sp=krnl_spmv_1.y:HBM[0]
```

这样语义上依然成立：

- `col_idx`、`values`、`x`、`y` 都能分配到 bank 0
- 每个 BO 仍然会有自己的地址范围

但坏处是：

- 都共享同一个 bank 的带宽
- AXI 仲裁冲突更多
- kernel 吞吐更容易下降

所以：

```text
“能不能放一起” 和 “应不应该放一起” 是两个问题
```

---

## 9. 当前工程里这几层的真实对应关系

可以把当前 demo 理解成这条链：

```text
host 里的 std::vector<int> col_idx
    ↓
make_bo(...)
    ↓
XRT 分配一个 BO
    ↓
这个 BO 被放到 col_idx 对应的 memory group
    ↓
cfg/u55c.cfg 把这个 group 接到 HBM[0]
    ↓
kernel 的 m_axi_gmem_col 端口去访问它
```

对 `values / x / y` 也是一样的。

---

## 10. 最后用一句话收住

这几个概念最容易混淆，但你可以这样记：

### 10.1 BO

```text
XRT 管理的一块设备内存对象
```

### 10.2 bank

```text
这块 BO 被分配到哪片 HBM 地址空间
```

### 10.3 起始地址

```text
这块 BO 在该 bank 内部具体从哪个地址开始，默认由 XRT 自动决定
```

### 10.4 kernel 指针参数

```text
不是 host 普通指针，而是“设备地址 + AXI 访问接口”的抽象
```

---

## 11. 推荐和哪些文档一起看

如果想把这件事完全看顺，建议配合：

- `/home/pyx/ProjectFS/Project-X/docs/C层到硬件实现_zh.md`
- `/home/pyx/ProjectFS/Project-X/docs/C层到硬件实现_图解版_zh.md`

这三份放一起看，基本就能把：

```text
host 指针
BO
group_id
HBM bank
kernel m_axi 端口
xclbin
```

这条链串起来。
