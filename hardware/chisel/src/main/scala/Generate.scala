package projectx

import chisel3.RawModule
import _root_.circt.stage.ChiselStage

import java.nio.charset.StandardCharsets
import java.nio.file.{Files, Path}

object ProjectXChiselEmitter {
  private val firtoolOpts = Array(
    "-disable-all-randomization",
    "-strip-debug-info",
    "-default-layer-specialization=enable"
  )

  def emitVerilog(gen: => RawModule, targetDir: String, fileName: String): Unit = {
    val outDir = Path.of(targetDir)
    Files.createDirectories(outDir)
    val systemVerilog = ChiselStage.emitSystemVerilog(
      gen = gen,
      firtoolOpts = firtoolOpts
    )
    Files.write(outDir.resolve(fileName), systemVerilog.getBytes(StandardCharsets.UTF_8))
  }
}

object GenerateMergeReducer extends App {
  ProjectXChiselEmitter.emitVerilog(
    gen = new MergeReducer(lanes = 3, dataWidth = 64),
    targetDir = "generated/merge",
    fileName = "MergeReducer.v"
  )
}

object GenerateSpmvRowEngine extends App {
  ProjectXChiselEmitter.emitVerilog(
    gen = new SpmvRowEngine(slotsPerRow = 3, dataWidth = 32, indexWidth = 16),
    targetDir = "generated/spmv",
    fileName = "SpmvRowEngine.v"
  )
}

object GenerateOuterProductMul extends App {
  ProjectXChiselEmitter.emitVerilog(
    gen = new OuterProductMul,
    targetDir = "generated/outer",
    fileName = "outer_product_mul_bits.v"
  )

  OuterProductMulFiles.write("generated/outer")
}

object GenerateAll extends App {
  GenerateMergeReducer.main(Array.empty)
  GenerateSpmvRowEngine.main(Array.empty)
  GenerateOuterProductMul.main(Array.empty)
}
