// 最小 U55C HLS kernel：ELLPACK 风格的稀疏矩阵向量乘法。
//
// 计算内容：
//   y[row] = scale * sum_k values[row, k] * x[col_idx[row, k]]
//
// 这里故意使用非常简单的稀疏矩阵格式，方便入门：
// - 每一行固定 kSlotsPerRow 个 slot
// - 无效 slot 使用 col_idx = -1, value = 0
// - host 当前生成一个三对角矩阵
//
// 这个文件是 kernel 架构的核心。完整 Alveo 设计还依赖 cfg/u55c.cfg，
// 因为 cfg/u55c.cfg 会把这些 m_axi 端口映射到真实的 U55C HBM bank。

extern "C" {

void krnl_spmv(int num_rows,
               double scale,
               const int* col_idx,
               const double* values,
               const double* x,
               double* y) {
// 下面这组 s_axilite pragma 会让 HLS 生成一个名为 control 的 AXI-Lite slave。
// 在 U55C 上，可以把它理解成“kernel 控制寄存器窗口”：
// - host 侧的 XRT 会通过 Alveo shell 的控制路径访问它
// - 标量参数会被写成寄存器值
// - 指针参数不会把 host 虚拟地址原样传进来，而是写入 device buffer 的基地址
//
// 也就是说：
// - 大块数据本体走下面的 m_axi -> HBM 路径
// - 这里只负责“控制”和“地址”
//
// num_rows 和 scale 是两个普通标量寄存器。
#pragma HLS INTERFACE s_axilite port = num_rows bundle = control
#pragma HLS INTERFACE s_axilite port = scale bundle = control

// 对指针参数，control bundle 这一侧承载的是 base address 寄存器。
// software/host.cpp 里传给 kernel 的 col_idx_bo / values_bo / x_bo / y_bo，
// 最终都会由 XRT 转成 device address，写到这些寄存器里。
#pragma HLS INTERFACE s_axilite port = col_idx bundle = control
#pragma HLS INTERFACE s_axilite port = values bundle = control
#pragma HLS INTERFACE s_axilite port = x bundle = control
#pragma HLS INTERFACE s_axilite port = y bundle = control

// return 不是“C++ 返回值接口”，而是标准 HLS kernel 控制寄存器集合的入口。
// 这一项会让 HLS 生成 ap_start / ap_done / ap_idle / ap_ready 等控制位；
// U55C 上 XRT 就是通过这些位启动 kernel、轮询/等待 kernel 完成。
#pragma HLS INTERFACE s_axilite port = return bundle = control

// 下面这组 m_axi pragma 会为四个数组各生成一个独立的 AXI4 master memory port。
//
// 关键点 1：bundle 名字不同，HLS 会尽量把它们做成不同的全局内存端口。
// 如果把多个数组写到同一个 bundle，它们会共享一套 AXI master，更容易互相抢带宽。
//
// 关键点 2：offset = slave 表示“这个 m_axi 端口的基地址来自上面的 control bundle”。
// 也就是：
//   host 传 BO -> XRT 把 device address 写进 control 寄存器
//   kernel 发访存 -> 通过这里的 AXI master 以该 base address 为起点访问外部内存
//
// 关键点 3：这些 bundle 名只是 kernel 内部接口名，不等于具体 HBM bank 名。
// 真正把端口连到 U55C 哪个 HBM bank / pseudo-channel，是 link 阶段由 cfg/u55c.cfg 决定的：
//   col_idx -> HBM[0]
//   values  -> HBM[1]
//   x       -> HBM[2]
//   y       -> HBM[3]
//
// 所以把当前设计放到 U55C 实物上看，大致就是：
//   kernel 内部有 4 条独立 AXI master 访存通路
//   经过平台互连后分别接到 4 个 HBM bank / pseudo-channel
// 这样比把所有数组都塞到同一条 global-memory 口更容易获得并行带宽。
//
// 读 col_idx[]：当前工程在 link 后把它接到 U55C 的 HBM[0]。
#pragma HLS INTERFACE m_axi port = col_idx offset = slave bundle = gmem_col
// 读 values[]：当前工程在 link 后把它接到 U55C 的 HBM[1]。
#pragma HLS INTERFACE m_axi port = values offset = slave bundle = gmem_val
// 读 x[]：当前工程在 link 后把它接到 U55C 的 HBM[2]。
#pragma HLS INTERFACE m_axi port = x offset = slave bundle = gmem_x
// 写 y[]：当前工程在 link 后把它接到 U55C 的 HBM[3]。
#pragma HLS INTERFACE m_axi port = y offset = slave bundle = gmem_y

    constexpr int kSlotsPerRow = 3;

RowLoop:
    for (int row = 0; row < num_rows; ++row) {
// 对 RowLoop 请求建立循环流水线，目标 II=1。
//
// 直观上它不是说“1 个周期算完 1 行”，而是说：
//   理想情况下，每个周期都能让一行新的 row 进入流水线，
//   同时前面若干行还在流水线的其他 stage 里继续执行。
//
// 对当前 U55C 设计，HLS 会尝试把以下工作组织成可重叠的流水级：
// - 读 col_idx / values
// - 按 col_idx 去读 x[col]
// - 做 FP64 乘法和累加
// - 把结果写到 y[row]
//
// 但 II=1 只是“目标”，不是保证；实际能否达到，主要受这些因素限制：
// 1. FP64 乘法器/加法器本身的流水线延迟与资源数
// 2. UNROLL 之后每行最多会触发 3 次 x[col] 读取，但它们都共享 gmem_x -> HBM[2] 这条口
// 3. AXI/HBM 的发射能力、仲裁和返回延迟
//
// 所以最终的实际 II，要以 HLS csynth 报告和 link 后报告为准。
#pragma HLS PIPELINE II=1

// 这个 pragma 主要是给 HLS 做性能/资源估算时提供循环迭代范围提示：
// - min=1, max=4096 对应当前 software/host.cpp 允许的 rows 范围
// - 它通常不会单独生成额外“限制 rows 必须 <= 4096”的硬件逻辑
// - 真正的运行时边界检查是在 software/host.cpp 的 parse_rows() 里做的
//
// 换句话说，它更多影响 report 里的 latency / throughput 估算，
// 而不是像 PIPELINE / UNROLL 那样直接改变核心数据通路结构。
#pragma HLS LOOP_TRIPCOUNT min=1 max=4096

        double acc = 0.0;

    SlotLoop:
        for (int slot = 0; slot < kSlotsPerRow; ++slot) {
// kSlotsPerRow 固定为 3，所以这里的 UNROLL 基本等价于“把 3 次迭代全部展开”。
//
// 在硬件上更接近这样的效果：
// - 不再保留一个只有 3 次迭代的小顺序循环控制器
// - HLS 会尝试复制 3 份 slot 计算逻辑
// - 然后把 3 个 slot 的部分结果汇总到 acc
//
// 对当前 kernel，每个 slot 大致包含：
//   idx 计算 -> 读 col_idx[idx] -> 读 values[idx] -> 条件判断 -> val * x[col]
//
// 但“展开成 3 路”不等于所有访存都一定同周期完成：
// - 3 次 col_idx 读取仍共享 gmem_col / HBM[0]
// - 3 次 values 读取仍共享 gmem_val / HBM[1]
// - 3 次 x[col] 读取仍共享 gmem_x / HBM[2]
//
// 所以 HLS 可能会复制算术 datapath，但仍按 AXI/HBM 的能力安排实际时序。
// 这也是为什么看 U55C 上的真实性能时，不能只看“写了 UNROLL”，
// 还要结合 memory port 报告、burst/issue 能力和最终 II 一起看。
#pragma HLS UNROLL
            const int idx = row * kSlotsPerRow + slot;
            const int col = col_idx[idx];
            const double val = values[idx];

            if (col >= 0) {
                acc += val * x[col];
            }
        }

        y[row] = scale * acc;
    }
}

}
