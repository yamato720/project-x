package projectx

import chisel3._

// 这个 BlackBox 不是让 Chisel 自己实现 FP64 乘法，而是声明一个
// Vivado 后面会生成的 Xilinx Floating Point IP wrapper。
//
// Chisel 只需要知道它的 Verilog 模块名和端口形状：
// - desiredName 必须和 Vivado create_ip 生成的 module_name 一致
// - 端口名必须和 floating_point IP 的 AXI-Stream 风格接口一致
//
// 这样 Chisel 生成的 outer_product_mul_bits.v 里会实例化
// outer_product_mul_dmul_ip，真正的乘法逻辑由 Vivado IP RTL 提供。
//
// 端口语义和 tile 版本里的乘法 IP 一样，只是这里服务的是单次标量乘法：
// - aclk / aclken: 时钟和时钟使能
// - s_axis_a_tvalid / s_axis_a_tdata: 左操作数有效和数据
// - s_axis_b_tvalid / s_axis_b_tdata: 右操作数有效和数据
// - m_axis_result_tvalid / m_axis_result_tdata: 结果有效和数据
class OuterProductDmulIp extends BlackBox {
  override def desiredName: String = "outer_product_mul_dmul_ip"

  val io = IO(new Bundle {
    val aclk = Input(Clock())
    val aclken = Input(Bool())
    val s_axis_a_tvalid = Input(Bool())
    val s_axis_a_tdata = Input(UInt(64.W))
    val s_axis_b_tvalid = Input(Bool())
    val s_axis_b_tdata = Input(UInt(64.W))
    val m_axis_result_tvalid = Output(Bool())
    val m_axis_result_tdata = Output(UInt(64.W))
  })
}

// HLS 只能通过一个 C 函数调用外部 RTL black-box，所以这里生成的顶层
// 模块名必须和 C 侧函数名、black-box JSON 里的 rtl_top_module_name 完全一致。
//
// C/HLS 看到的函数是：
//   outer_product_mul_bits(lhs_bits, rhs_bits, result_bits)
//
// RTL 侧暴露的是 HLS 认识的 ap_ctrl_chain 控制协议：
// - ap_start：HLS 发起一次函数调用
// - ap_done：RTL 告诉 HLS 本次调用结果有效
// - ap_ready/ap_idle/ap_continue：配合 HLS 调度和链式控制
//
// 数据端口故意使用 UInt(64.W)，而不是 Chisel/Verilog 的 real/double：
// HLS black-box 对整数位宽端口的映射最直接，64 bit 内容按 IEEE-754 double
// bit pattern 透传给 Xilinx Floating Point IP。
//
// 这个单元素版本是外积早期接法的保留实现。当前 Project-X 的
// hardware/chisel_core/krnl_spmv.cpp 和 hardware/hybrid/krnl_spmv.cpp
// 已经切到 outer_product_tile_bits(...)，仓库里没有活跃 kernel 再直接调用
// outer_product_mul_bits(...)。保留它的原因主要有两个：
// 1. 作为最小可读示例，方便理解 HLS black-box 到单颗 FP IP 的接法
// 2. 如果以后要回归逐元素调用或做对比实验，这套生成链仍然能直接使用
//
// 参数/端口含义：
// - lhs_bits: 一个 lhs 标量，按 IEEE-754 double bit pattern 编码
// - rhs_bits: 一个 rhs 标量，按 IEEE-754 double bit pattern 编码
// - result_bits: 乘法结果 bit pattern
// - result_bits_ap_vld: result_bits 对 HLS 可见的有效信号
class OuterProductMul extends RawModule {
  override def desiredName: String = "outer_product_mul_bits"

  // 这里必须和 OuterProductMulFiles.scala 里生成的 JSON latency，以及
  // create_outer_product_mul_dmul_ip.tcl 里配置的 CONFIG.c_latency 保持一致。
  // 如果以后改 FP IP 延迟，这三个地方要一起改，否则 HLS 调度会认为结果
  // 更早/更晚到达，硬件行为可能不匹配。
  private val fpLatency = 6

  // 下面这些端口名是 HLS black-box JSON 直接引用的“接口契约”。
  // 改名后必须同步改 outer_product_mul.json 的 rtl_common_signal / rtl_ports。
  val ap_clk = IO(Input(Clock()))
  val ap_rst = IO(Input(Bool()))
  val ap_ce = IO(Input(Bool()))
  val ap_start = IO(Input(Bool()))
  val ap_continue = IO(Input(Bool()))
  val lhs_bits = IO(Input(UInt(64.W)))
  val rhs_bits = IO(Input(UInt(64.W)))

  val ap_idle = IO(Output(Bool()))
  val ap_done = IO(Output(Bool()))
  val ap_ready = IO(Output(Bool()))
  val result_bits_ap_vld = IO(Output(Bool()))
  val result_bits = IO(Output(UInt(64.W)))

  // 单次 HLS 函数调用会被翻译成一次底层 FP multiply IP 请求。
  // 把 HLS 的一次函数调用翻译成 Floating Point IP 的一次 AXI-Stream 输入。
  // 当前 IP 配置为 NonBlocking 且没有 tready，因此 ap_start 同时作为两个
  // 输入通道的 tvalid；ap_ce 负责在 HLS 停顿时冻结 IP 的时钟使能。
  val dmul = Module(new OuterProductDmulIp)
  dmul.io.aclk := ap_clk
  dmul.io.aclken := ap_ce
  dmul.io.s_axis_a_tvalid := ap_start
  dmul.io.s_axis_a_tdata := lhs_bits
  dmul.io.s_axis_b_tvalid := ap_start
  dmul.io.s_axis_b_tdata := rhs_bits

  withClockAndReset(ap_clk, ap_rst) {
    // Floating Point IP 的输出会在固定 latency 后出现。
    // 这里用 validPipe 对齐“哪一次 ap_start 对应哪一次结果”，再和 IP 的
    // m_axis_result_tvalid、HLS 的 ap_continue 一起决定 result_bits 是否有效。
    val validPipe = RegInit(VecInit(Seq.fill(fpLatency)(false.B)))

    when(ap_ce) {
      validPipe(0) := ap_start
      for (stage <- 1 until fpLatency) {
        validPipe(stage) := validPipe(stage - 1)
      }
    }

    val resultValid = validPipe(fpLatency - 1) && dmul.io.m_axis_result_tvalid && ap_continue
    ap_done := resultValid
    result_bits_ap_vld := resultValid
  }

  // HLS 当前没有把这个 black-box 放进 pipeline loop，所以这里给出一个简单
  // ready/idle 行为即可满足调度。之前硬件编译失败的原因正是 ap_ctrl_chain
  // black-box 不能出现在 HLS pipeline region 里；C++ 侧已经显式关闭外积
  // 内层循环 pipeline。
  ap_ready := ap_start
  ap_idle := !ap_start
  result_bits := dmul.io.m_axis_result_tdata
}
