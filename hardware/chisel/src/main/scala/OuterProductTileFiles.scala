package projectx

import java.nio.charset.StandardCharsets
import java.nio.file.{Files, Path}

object OuterProductTileFiles {
  private val tileSize = 8
  private val outputCount = tileSize * tileSize
  private val lhsNames = Seq.tabulate(tileSize)(i => s"lhs${i}_bits")
  private val rhsNames = Seq.tabulate(tileSize)(i => s"rhs${i}_bits")
  private val outputNames = Seq.tabulate(outputCount)(i => s"out${i}_bits")
  private val inputNames = lhsNames ++ rhsNames

  def write(targetDir: String): Unit = {
    val dir = Path.of(targetDir).toAbsolutePath.normalize()
    Files.createDirectories(dir)
    writeFile(dir.resolve("outer_product_tile.hpp"), header)
    writeFile(dir.resolve("outer_product_tile_model.cpp"), model)
    writeFile(dir.resolve("outer_product_tile.json"), json(dir))
    writeFile(dir.resolve("create_outer_product_tile_dmul_ip.tcl"), ipTcl(dir))
  }

  private def writeFile(path: Path, text: String): Unit =
    Files.write(path, text.getBytes(StandardCharsets.UTF_8))

  private def renderMacro(name: String, lines: Seq[String]): String = {
    val prefix = s"#define $name "
    if (lines.isEmpty) {
      prefix
    } else {
      val body = lines.zipWithIndex.map { case (line, index) =>
        val suffix = if (index == lines.length - 1) "" else " \\"
        s"    $line$suffix"
      }
      (prefix + "\\") + "\n" + body.mkString("\n")
    }
  }

  private def declarationArgs: Seq[String] =
    inputNames.map(name => s"unsigned long long $name") ++
      outputNames.map(name => s"unsigned long long& $name")

  private def functionDeclaration(name: String): String =
    declarationArgs.mkString(s"void $name(", ",\n                            ", ");")

  private val wrapperCall: String = {
    val args =
      (0 until tileSize).map(i => s"(lhs_bits)[$i]") ++
        (0 until tileSize).map(i => s"(rhs_bits)[$i]") ++
        outputNames.map(name => s"prefix##_$name")
    args.mkString("outer_product_tile_bits(", ",\n                            ", ");")
  }

  private val header: String = {
    val inputMacro = renderMacro("PROJECTX_OUTER_PRODUCT_TILE_INPUT_PARAMS", inputNames.map(name => s"unsigned long long $name,").updated(inputNames.length - 1, s"unsigned long long ${inputNames.last}"))
    val outputMacro = renderMacro("PROJECTX_OUTER_PRODUCT_TILE_OUTPUT_PARAMS", outputNames.map(name => s"unsigned long long& $name,").updated(outputNames.length - 1, s"unsigned long long& ${outputNames.last}"))
    val outputPtrMacro = renderMacro("PROJECTX_OUTER_PRODUCT_TILE_OUTPUT_PTRS", outputNames.map(name => s"&$name,").updated(outputNames.length - 1, s"&${outputNames.last}"))
    val declareOutputVars = renderMacro(
      "PROJECTX_OUTER_PRODUCT_TILE_DECLARE_OUTPUTS(prefix)",
      outputNames.map(name => s"unsigned long long prefix##_$name = 0;")
    )
    val copyOutputVars = renderMacro(
      "PROJECTX_OUTER_PRODUCT_TILE_COPY_OUTPUTS(prefix, out_bits)",
      outputNames.zipWithIndex.map { case (name, index) =>
        s"(out_bits)[$index] = prefix##_$name;"
      }
    )
    val callMacro = renderMacro(
      "PROJECTX_OUTER_PRODUCT_TILE_CALL(lhs_bits, rhs_bits, prefix)",
      wrapperCall.split('\n').toSeq
    )
    val lhsValues = lhsNames.map(name => s"        projectx_tile_bits_to_double($name)").mkString(",\n")
    val rhsValues = rhsNames.map(name => s"        projectx_tile_bits_to_double($name)").mkString(",\n")

    s"""#ifndef PROJECTX_OUTER_PRODUCT_TILE_HPP
      |#define PROJECTX_OUTER_PRODUCT_TILE_HPP
      |
      |$inputMacro
      |
      |$outputMacro
      |
      |$outputPtrMacro
      |
      |$declareOutputVars
      |
      |$callMacro
      |
      |$copyOutputVars
      |
      |${functionDeclaration("outer_product_tile_bits")}
      |
      |#ifndef PROJECTX_OUTER_PRODUCT_TILE_NO_INLINE_MODEL
      |inline unsigned long long projectx_tile_double_to_bits(double value) {
      |    union {
      |        double d;
      |        unsigned long long u;
      |    } convert;
      |    convert.d = value;
      |    return convert.u;
      |}
      |
      |inline double projectx_tile_bits_to_double(unsigned long long value) {
      |    union {
      |        double d;
      |        unsigned long long u;
      |    } convert;
      |    convert.u = value;
      |    return convert.d;
      |}
      |
      |inline void outer_product_tile_bits(
      |    PROJECTX_OUTER_PRODUCT_TILE_INPUT_PARAMS,
      |    PROJECTX_OUTER_PRODUCT_TILE_OUTPUT_PARAMS) {
      |    const double lhs[$tileSize] = {
      |$lhsValues
      |    };
      |    const double rhs[$tileSize] = {
      |$rhsValues
      |    };
      |    unsigned long long* out_ptrs[$outputCount] = {
      |        PROJECTX_OUTER_PRODUCT_TILE_OUTPUT_PTRS
      |    };
      |
      |    for (int row = 0; row < $tileSize; ++row) {
      |        for (int col = 0; col < $tileSize; ++col) {
      |            *out_ptrs[row * $tileSize + col] =
      |                projectx_tile_double_to_bits(lhs[row] * rhs[col]);
      |        }
      |    }
      |}
      |#endif
      |
      |#endif
      |""".stripMargin
  }

  private val model: String =
    s"""#define PROJECTX_OUTER_PRODUCT_TILE_NO_INLINE_MODEL
      |#include "outer_product_tile.hpp"
      |
      |namespace {
      |
      |unsigned long long tile_double_to_bits(double value) {
      |    union {
      |        double d;
      |        unsigned long long u;
      |    } convert;
      |    convert.d = value;
      |    return convert.u;
      |}
      |
      |double tile_bits_to_double(unsigned long long value) {
      |    union {
      |        double d;
      |        unsigned long long u;
      |    } convert;
      |    convert.u = value;
      |    return convert.d;
      |}
      |
      |} // namespace
      |
      |void outer_product_tile_bits(
      |    PROJECTX_OUTER_PRODUCT_TILE_INPUT_PARAMS,
      |    PROJECTX_OUTER_PRODUCT_TILE_OUTPUT_PARAMS) {
      |    const double lhs[$tileSize] = {
      |${lhsNames.map(name => s"        tile_bits_to_double($name)").mkString(",\n")}
      |    };
      |    const double rhs[$tileSize] = {
      |${rhsNames.map(name => s"        tile_bits_to_double($name)").mkString(",\n")}
      |    };
      |    unsigned long long* out_ptrs[$outputCount] = {
      |        PROJECTX_OUTER_PRODUCT_TILE_OUTPUT_PTRS
      |    };
      |
      |    for (int row = 0; row < $tileSize; ++row) {
      |        for (int col = 0; col < $tileSize; ++col) {
      |            *out_ptrs[row * $tileSize + col] =
      |                tile_double_to_bits(lhs[row] * rhs[col]);
      |        }
      |    }
      |}
      |""".stripMargin

  private def inputParam(name: String): String =
    s"""    {
      |      "c_name": ${jsonString(name)},
      |      "c_port_direction": "in",
      |      "rtl_ports": {
      |        "data_read_in": ${jsonString(name)}
      |      }
      |    }""".stripMargin

  private def outputParam(name: String): String =
    s"""    {
      |      "c_name": ${jsonString(name)},
      |      "c_port_direction": "out",
      |      "rtl_ports": {
      |        "data_write_out": ${jsonString(name)},
      |        "data_write_valid": ${jsonString(name + "_ap_vld")}
      |      }
      |    }""".stripMargin

  private def json(dir: Path): String = {
    val modelFile = jsonString(dir.resolve("outer_product_tile_model.cpp").toString)
    val wrapperFile = jsonString(dir.resolve("outer_product_tile_bits.v").toString)
    val ipWrapperFile = jsonString(dir.resolve("ip/outer_product_tile_dmul_ip/synth/outer_product_tile_dmul_ip.v").toString)
    val ipRfsFile = jsonString(dir.resolve("ip/outer_product_tile_dmul_ip/hdl/floating_point_v7_1_rfs.v").toString)
    val parameters = (inputNames.map(inputParam) ++ outputNames.map(outputParam)).mkString(",\n")

    s"""{
      |  "c_function_name": "outer_product_tile_bits",
      |  "rtl_top_module_name": "outer_product_tile_bits",
      |  "c_files": [
      |    {
      |      "c_file": $modelFile,
      |      "cflag": ${jsonString("-I" + dir.toString)}
      |    }
      |  ],
      |  "rtl_files": [
      |    $wrapperFile,
      |    $ipWrapperFile,
      |    $ipRfsFile
      |  ],
      |  "c_parameters": [
      |$parameters
      |  ],
      |  "rtl_common_signal": {
      |    "module_clock": "ap_clk",
      |    "module_reset": "ap_rst",
      |    "module_clock_enable": "ap_ce",
      |    "ap_ctrl_chain_protocol_idle": "ap_idle",
      |    "ap_ctrl_chain_protocol_start": "ap_start",
      |    "ap_ctrl_chain_protocol_ready": "ap_ready",
      |    "ap_ctrl_chain_protocol_done": "ap_done",
      |    "ap_ctrl_chain_protocol_continue": "ap_continue"
      |  },
      |  "rtl_performance": {
      |    "latency": "6",
      |    "II": "1"
      |  },
      |  "rtl_resource_usage": {
      |    "FF": "0",
      |    "LUT": "0",
      |    "BRAM": "0",
      |    "URAM": "0",
      |    "DSP": "704"
      |  }
      |}
      |""".stripMargin
  }

  private def ipTcl(dir: Path): String = {
    val ipDir = dir.resolve("ip").toString
    s"""set ip_dir [file normalize ${tclString(ipDir)}]
      |file mkdir $$ip_dir
      |create_project -in_memory -part xcu55c-fsvh2892-2L-e
      |
      |set vivado_ver [version -short]
      |set fpo_ver 7.1
      |if {[regexp -nocase {2015\\.1.*} $$vivado_ver match]} {
      |    set fpo_ver 7.0
      |}
      |
      |create_ip -name floating_point -version $$fpo_ver -vendor xilinx.com -library ip -module_name outer_product_tile_dmul_ip -dir $$ip_dir -force
      |set_property -dict [list CONFIG.a_precision_type Double \\
      |                          CONFIG.a_tuser_width 1 \\
      |                          CONFIG.add_sub_value Both \\
      |                          CONFIG.b_tuser_width 1 \\
      |                          CONFIG.c_a_exponent_width 11 \\
      |                          CONFIG.c_a_fraction_width 53 \\
      |                          CONFIG.c_compare_operation Programmable \\
      |                          CONFIG.c_has_divide_by_zero false \\
      |                          CONFIG.c_has_invalid_op false \\
      |                          CONFIG.c_has_overflow false \\
      |                          CONFIG.c_has_underflow false \\
      |                          CONFIG.c_latency 6 \\
      |                          CONFIG.c_mult_usage Max_Usage \\
      |                          CONFIG.c_optimization Speed_Optimized \\
      |                          CONFIG.c_rate 1 \\
      |                          CONFIG.c_result_exponent_width 11 \\
      |                          CONFIG.c_result_fraction_width 53 \\
      |                          CONFIG.component_name outer_product_tile_dmul_ip \\
      |                          CONFIG.flow_control NonBlocking \\
      |                          CONFIG.has_a_tlast false \\
      |                          CONFIG.has_a_tuser false \\
      |                          CONFIG.has_aclken true \\
      |                          CONFIG.has_aresetn false \\
      |                          CONFIG.has_b_tlast false \\
      |                          CONFIG.has_b_tuser false \\
      |                          CONFIG.has_operation_tlast false \\
      |                          CONFIG.has_operation_tuser false \\
      |                          CONFIG.has_result_tready false \\
      |                          CONFIG.maximum_latency false \\
      |                          CONFIG.operation_tuser_width 1 \\
      |                          CONFIG.operation_type Multiply \\
      |                          CONFIG.result_precision_type Double \\
      |                          CONFIG.result_tlast_behv Null] [get_ips outer_product_tile_dmul_ip] -quiet
      |set_property generate_synth_checkpoint false [get_files outer_product_tile_dmul_ip.xci]
      |generate_target {synthesis simulation} [get_files outer_product_tile_dmul_ip.xci]
      |close_project
      |""".stripMargin
  }

  private def jsonString(value: String): String =
    "\"" + value.flatMap {
      case '\\' => "\\\\"
      case '"' => "\\\""
      case c => c.toString
    } + "\""

  private def tclString(value: String): String =
    "{" + value + "}"
}
