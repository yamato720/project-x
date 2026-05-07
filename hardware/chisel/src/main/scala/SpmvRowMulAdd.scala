package projectx

import chisel3._
import chisel3.util.ShiftRegister

class SpmvRowDmulIp extends BlackBox {
  override def desiredName: String = "spmv_row_dmul_ip"

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

class SpmvRowDaddIp extends BlackBox {
  override def desiredName: String = "spmv_row_dadd_ip"

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

// HLS black-box 顶层：
//   spmv_row_muladd_bits(scale, val0, x0, val1, x1, val2, x2, result)
//
// HLS wrapper 负责从 HBM 读取 col_idx/values/x，并把无效 slot 置零；
// 这个 Chisel/Vivado IP wrapper 只负责 FP64 compute pipeline：
//   result = scale * (val0*x0 + val1*x1 + val2*x2)
class SpmvRowMulAdd extends RawModule {
  override def desiredName: String = "spmv_row_muladd_bits"

  private val fpLatency = 6
  private val totalLatency = fpLatency * 4

  val ap_clk = IO(Input(Clock()))
  val ap_rst = IO(Input(Bool()))
  val ap_ce = IO(Input(Bool()))
  val ap_start = IO(Input(Bool()))
  val ap_continue = IO(Input(Bool()))

  val scale_bits = IO(Input(UInt(64.W)))
  val val0_bits = IO(Input(UInt(64.W)))
  val x0_bits = IO(Input(UInt(64.W)))
  val val1_bits = IO(Input(UInt(64.W)))
  val x1_bits = IO(Input(UInt(64.W)))
  val val2_bits = IO(Input(UInt(64.W)))
  val x2_bits = IO(Input(UInt(64.W)))

  val ap_idle = IO(Output(Bool()))
  val ap_done = IO(Output(Bool()))
  val ap_ready = IO(Output(Bool()))
  val result_bits_ap_vld = IO(Output(Bool()))
  val result_bits = IO(Output(UInt(64.W)))

  private def connectMul(ip: SpmvRowDmulIp, lhs: UInt, rhs: UInt, valid: Bool): Unit = {
    ip.io.aclk := ap_clk
    ip.io.aclken := ap_ce
    ip.io.s_axis_a_tvalid := valid
    ip.io.s_axis_a_tdata := lhs
    ip.io.s_axis_b_tvalid := valid
    ip.io.s_axis_b_tdata := rhs
  }

  private def connectAdd(ip: SpmvRowDaddIp, lhs: UInt, rhs: UInt, valid: Bool): Unit = {
    ip.io.aclk := ap_clk
    ip.io.aclken := ap_ce
    ip.io.s_axis_a_tvalid := valid
    ip.io.s_axis_a_tdata := lhs
    ip.io.s_axis_b_tvalid := valid
    ip.io.s_axis_b_tdata := rhs
  }

  val mul0 = Module(new SpmvRowDmulIp)
  val mul1 = Module(new SpmvRowDmulIp)
  val mul2 = Module(new SpmvRowDmulIp)
  connectMul(mul0, val0_bits, x0_bits, ap_start)
  connectMul(mul1, val1_bits, x1_bits, ap_start)
  connectMul(mul2, val2_bits, x2_bits, ap_start)

  val add01 = Module(new SpmvRowDaddIp)
  connectAdd(add01,
             mul0.io.m_axis_result_tdata,
             mul1.io.m_axis_result_tdata,
             mul0.io.m_axis_result_tvalid && mul1.io.m_axis_result_tvalid)

  withClockAndReset(ap_clk, ap_rst) {
    val mul2Delayed = ShiftRegister(mul2.io.m_axis_result_tdata, fpLatency, 0.U(64.W), ap_ce)
    val mul2ValidDelayed = ShiftRegister(mul2.io.m_axis_result_tvalid, fpLatency, false.B, ap_ce)
    val scaleDelayed = ShiftRegister(scale_bits, fpLatency * 3, 0.U(64.W), ap_ce)

    val add012 = Module(new SpmvRowDaddIp)
    connectAdd(add012,
               add01.io.m_axis_result_tdata,
               mul2Delayed,
               add01.io.m_axis_result_tvalid && mul2ValidDelayed)

    val scaleMul = Module(new SpmvRowDmulIp)
    connectMul(scaleMul,
               add012.io.m_axis_result_tdata,
               scaleDelayed,
               add012.io.m_axis_result_tvalid)

    val validPipe = RegInit(VecInit(Seq.fill(totalLatency)(false.B)))
    when(ap_ce) {
      validPipe(0) := ap_start
      for (stage <- 1 until totalLatency) {
        validPipe(stage) := validPipe(stage - 1)
      }
    }

    val resultValid = validPipe(totalLatency - 1) && scaleMul.io.m_axis_result_tvalid && ap_continue
    ap_done := resultValid
    result_bits_ap_vld := resultValid
    result_bits := scaleMul.io.m_axis_result_tdata
  }

  ap_ready := ap_start
  ap_idle := !ap_start
}
