package projectx

import java.nio.charset.StandardCharsets
import java.nio.file.{Files, Path}

object SpmvRowMulAddFiles {
  def write(targetDir: String): Unit = {
    val dir = Path.of(targetDir).toAbsolutePath.normalize()
    Files.createDirectories(dir)
    writeFile(dir.resolve("spmv_row_muladd.hpp"), header)
    writeFile(dir.resolve("spmv_row_muladd_model.cpp"), model)
    writeFile(dir.resolve("spmv_row_muladd.json"), json(dir))
    writeFile(dir.resolve("create_spmv_row_fp_ips.tcl"), ipTcl(dir))
  }

  private def writeFile(path: Path, text: String): Unit =
    Files.write(path, text.getBytes(StandardCharsets.UTF_8))

  private val header: String =
    """#ifndef PROJECTX_SPMV_ROW_MULADD_HPP
      |#define PROJECTX_SPMV_ROW_MULADD_HPP
      |
      |void spmv_row_muladd_bits(unsigned long long scale_bits,
      |                           unsigned long long val0_bits,
      |                           unsigned long long x0_bits,
      |                           unsigned long long val1_bits,
      |                           unsigned long long x1_bits,
      |                           unsigned long long val2_bits,
      |                           unsigned long long x2_bits,
      |                           unsigned long long& result_bits);
      |
      |#endif
      |""".stripMargin

  private val model: String =
    """#include "spmv_row_muladd.hpp"
      |
      |namespace {
      |
      |unsigned long long row_double_to_bits(double value) {
      |    union {
      |        double d;
      |        unsigned long long u;
      |    } convert;
      |    convert.d = value;
      |    return convert.u;
      |}
      |
      |double row_bits_to_double(unsigned long long value) {
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
      |void spmv_row_muladd_bits(unsigned long long scale_bits,
      |                           unsigned long long val0_bits,
      |                           unsigned long long x0_bits,
      |                           unsigned long long val1_bits,
      |                           unsigned long long x1_bits,
      |                           unsigned long long val2_bits,
      |                           unsigned long long x2_bits,
      |                           unsigned long long& result_bits) {
      |#pragma HLS inline off
      |    const double scale = row_bits_to_double(scale_bits);
      |    const double acc =
      |        row_bits_to_double(val0_bits) * row_bits_to_double(x0_bits) +
      |        row_bits_to_double(val1_bits) * row_bits_to_double(x1_bits) +
      |        row_bits_to_double(val2_bits) * row_bits_to_double(x2_bits);
      |    result_bits = row_double_to_bits(scale * acc);
      |}
      |""".stripMargin

  private def json(dir: Path): String = {
    val modelFile = jsonString(dir.resolve("spmv_row_muladd_model.cpp").toString)
    val wrapperFile = jsonString(dir.resolve("spmv_row_muladd_bits.v").toString)
    val mulWrapperFile = jsonString(dir.resolve("ip/spmv_row_dmul_ip/synth/spmv_row_dmul_ip.v").toString)
    val addWrapperFile = jsonString(dir.resolve("ip/spmv_row_dadd_ip/synth/spmv_row_dadd_ip.v").toString)
    val parameters = Seq(
      inputParam("scale_bits"),
      inputParam("val0_bits"),
      inputParam("x0_bits"),
      inputParam("val1_bits"),
      inputParam("x1_bits"),
      inputParam("val2_bits"),
      inputParam("x2_bits")
    ).mkString(",\n")

    s"""{
      |  "c_function_name": "spmv_row_muladd_bits",
      |  "rtl_top_module_name": "spmv_row_muladd_bits",
      |  "c_files": [
      |    {
      |      "c_file": $modelFile,
      |      "cflag": ${jsonString("-I" + dir.toString)}
      |    }
      |  ],
      |  "rtl_files": [
      |    $wrapperFile,
      |    $mulWrapperFile,
      |    $addWrapperFile
      |  ],
      |  "c_parameters": [
      |$parameters,
      |    {
      |      "c_name": "result_bits",
      |      "c_port_direction": "out",
      |      "rtl_ports": {
      |        "data_write_out": "result_bits",
      |        "data_write_valid": "result_bits_ap_vld"
      |      }
      |    }
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
      |    "latency": "24",
      |    "II": "1"
      |  },
      |  "rtl_resource_usage": {
      |    "FF": "0",
      |    "LUT": "0",
      |    "BRAM": "0",
      |    "URAM": "0",
      |    "DSP": "64"
      |  }
      |}
      |""".stripMargin
  }

  private def inputParam(name: String): String =
    s"""    {
      |      "c_name": ${jsonString(name)},
      |      "c_port_direction": "in",
      |      "rtl_ports": {
      |        "data_read_in": ${jsonString(name)}
      |      }
      |    }""".stripMargin

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
      |create_ip -name floating_point -version $$fpo_ver -vendor xilinx.com -library ip -module_name spmv_row_dmul_ip -dir $$ip_dir -force
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
      |                          CONFIG.component_name spmv_row_dmul_ip \\
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
      |                          CONFIG.result_tlast_behv Null] [get_ips spmv_row_dmul_ip] -quiet
      |set_property generate_synth_checkpoint false [get_files spmv_row_dmul_ip.xci]
      |generate_target {synthesis simulation} [get_files spmv_row_dmul_ip.xci]
      |
      |create_ip -name floating_point -version $$fpo_ver -vendor xilinx.com -library ip -module_name spmv_row_dadd_ip -dir $$ip_dir -force
      |set_property -dict [list CONFIG.a_precision_type Double \\
      |                          CONFIG.a_tuser_width 1 \\
      |                          CONFIG.add_sub_value Add \\
      |                          CONFIG.b_tuser_width 1 \\
      |                          CONFIG.c_a_exponent_width 11 \\
      |                          CONFIG.c_a_fraction_width 53 \\
      |                          CONFIG.c_compare_operation Programmable \\
      |                          CONFIG.c_has_divide_by_zero false \\
      |                          CONFIG.c_has_invalid_op false \\
      |                          CONFIG.c_has_overflow false \\
      |                          CONFIG.c_has_underflow false \\
      |                          CONFIG.c_latency 6 \\
      |                          CONFIG.c_mult_usage Full_Usage \\
      |                          CONFIG.c_optimization Speed_Optimized \\
      |                          CONFIG.c_rate 1 \\
      |                          CONFIG.c_result_exponent_width 11 \\
      |                          CONFIG.c_result_fraction_width 53 \\
      |                          CONFIG.component_name spmv_row_dadd_ip \\
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
      |                          CONFIG.operation_type Add_Subtract \\
      |                          CONFIG.result_precision_type Double \\
      |                          CONFIG.result_tlast_behv Null] [get_ips spmv_row_dadd_ip] -quiet
      |set_property generate_synth_checkpoint false [get_files spmv_row_dadd_ip.xci]
      |generate_target {synthesis simulation} [get_files spmv_row_dadd_ip.xci]
      |
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
