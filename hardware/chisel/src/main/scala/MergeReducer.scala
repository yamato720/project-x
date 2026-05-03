package projectx

import chisel3._
import chisel3.util._

class MergeReducer(val lanes: Int, val dataWidth: Int) extends Module {
  require(lanes > 0, "lanes must be positive")
  require(dataWidth > 0, "dataWidth must be positive")

  val io = IO(new Bundle {
    val in = Input(Vec(lanes, Valid(SInt(dataWidth.W))))
    val out = Output(Valid(SInt()))
  })

  private def reduceTree(nodes: Seq[SInt]): SInt = {
    nodes.length match {
      case 1 => nodes.head
      case _ =>
        reduceTree(nodes.grouped(2).map {
          case Seq(a, b) => a + b
          case Seq(a) => a
        }.toSeq)
    }
  }

  private val maskedInputs = io.in.map(port => Mux(port.valid, port.bits, 0.S(dataWidth.W)))

  io.out.valid := io.in.map(_.valid).reduce(_ || _)
  io.out.bits := reduceTree(maskedInputs)
}
