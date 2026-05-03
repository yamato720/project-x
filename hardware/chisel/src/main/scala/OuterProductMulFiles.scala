package projectx

import java.nio.charset.StandardCharsets
import java.nio.file.{Files, Path}

object OuterProductMulFiles {
  // OuterProductMul.scala 只负责生成 RTL wrapper。HLS 接入还需要几份
  // companion 文件，所以集中在这里由同一个 generator 生成，避免手工文件
  // 和 Verilog 端口名漂移：
  // - .hpp：给 krnl_spmv.cpp 声明 C 函数和 bits/double 转换工具
  // - _model.cpp：给 HLS/sim 使用的 C model
  // - .json：Vitis HLS RTL black-box 描述文件
  // - .tcl：调用 Vivado 生成 Xilinx Floating Point IP RTL
  def write(targetDir: String): Unit = {
    val dir = Path.of(targetDir).toAbsolutePath.normalize()
    Files.createDirectories(dir)
    writeFile(dir.resolve("outer_product_mul.hpp"), header)
    writeFile(dir.resolve("outer_product_mul_model.cpp"), model)
    writeFile(dir.resolve("outer_product_mul.json"), json(dir))
    writeFile(dir.resolve("create_outer_product_mul_dmul_ip.tcl"), ipTcl(dir))
  }

  private def writeFile(path: Path, text: String): Unit =
    Files.write(path, text.getBytes(StandardCharsets.UTF_8))

  private val header: String =
    """#ifndef PROJECTX_OUTER_PRODUCT_MUL_HPP
      |#define PROJECTX_OUTER_PRODUCT_MUL_HPP
      |
      |// HLS <-> RTL black-box 的 C/C++ 侧声明文件。
      |// HLS kernel 用 unsigned long long 传递 double 的原始 64-bit bit pattern，
      |// RTL wrapper 再把这些 bit 接到 Xilinx Floating Point FP64 multiply IP。
      |
      |inline unsigned long long projectx_double_to_bits(double value) {
      |    union {
      |        double d;
      |        unsigned long long u;
      |    } convert;
      |    convert.d = value;
      |    return convert.u;
      |}
      |
      |inline double projectx_bits_to_double(unsigned long long value) {
      |    union {
      |        double d;
      |        unsigned long long u;
      |    } convert;
      |    convert.u = value;
      |    return convert.d;
      |}
      |
      |// 这个函数没有在普通 C++ 源里实现为硬件逻辑。
      |// 综合时它由 outer_product_mul.json 绑定到 RTL 顶层 outer_product_mul_bits。
      |void outer_product_mul_bits(unsigned long long lhs_bits,
      |                            unsigned long long rhs_bits,
      |                            unsigned long long& result_bits);
      |
      |#endif
      |""".stripMargin

  // C model 是 black-box 的软件等价实现。综合时 HLS 会使用 RTL black-box；
  // 非综合/软件仿真路径需要这个函数有一个 C++ 定义，否则运行库可能缺符号。
  private val model: String =
    """#include "outer_product_mul.hpp"
      |
      |// 软件模型：给 HLS C simulation / sw_emu 等非 RTL 路径使用。
      |// 硬件综合时会使用 black-box JSON 指向的 Chisel/Vivado IP RTL。
      |void outer_product_mul_bits(unsigned long long lhs_bits,
      |                            unsigned long long rhs_bits,
      |                            unsigned long long& result_bits) {
      |#pragma HLS inline off
      |    const double lhs = projectx_bits_to_double(lhs_bits);
      |    const double rhs = projectx_bits_to_double(rhs_bits);
      |    result_bits = projectx_double_to_bits(lhs * rhs);
      |}
      |""".stripMargin

  private def json(dir: Path): String = {
    val includeDir = jsonString(dir.toString)
    val modelFile = jsonString(dir.resolve("outer_product_mul_model.cpp").toString)
    val wrapperFile = jsonString(dir.resolve("outer_product_mul_bits.v").toString)
    val ipWrapperFile = jsonString(dir.resolve("ip/outer_product_mul_dmul_ip/synth/outer_product_mul_dmul_ip.v").toString)
    val ipRfsFile = jsonString(dir.resolve("ip/outer_product_mul_dmul_ip/hdl/floating_point_v7_1_rfs.v").toString)

    // 这是 Vitis HLS 认识 RTL black-box 的核心文件：
    // - c_function_name 对应 krnl_spmv.cpp 里的函数调用名
    // - rtl_top_module_name 对应 Chisel 生成的 Verilog 顶层名
    // - c_parameters 把 C 参数映射到 RTL 数据端口
    // - rtl_common_signal 告诉 HLS clock/reset/ap_ctrl_chain 信号名
    // - rtl_files 把 Chisel wrapper、Vivado IP wrapper、IP 内部 RTL 全部交给 HLS
    //
    // JSON 标准不能写注释，所以说明只能放在 Scala generator 里。
    s"""{
      |  "c_function_name": "outer_product_mul_bits",
      |  "rtl_top_module_name": "outer_product_mul_bits",
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
      |    {
      |      "c_name": "lhs_bits",
      |      "c_port_direction": "in",
      |      "rtl_ports": {
      |        "data_read_in": "lhs_bits"
      |      }
      |    },
      |    {
      |      "c_name": "rhs_bits",
      |      "c_port_direction": "in",
      |      "rtl_ports": {
      |        "data_read_in": "rhs_bits"
      |      }
      |    },
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
      |    "latency": "6",
      |    "II": "1"
      |  },
      |  "rtl_resource_usage": {
      |    "FF": "0",
      |    "LUT": "0",
      |    "BRAM": "0",
      |    "URAM": "0",
      |    "DSP": "11"
      |  }
      |}
      |""".stripMargin
  }

  private def ipTcl(dir: Path): String = {
    val ipDir = dir.resolve("ip").toString
    // 这里生成的是 Vivado batch Tcl，而不是 HLS Tcl。Makefile 会在 v++ -c
    // 前先跑它，把 outer_product_mul_dmul_ip 的综合/仿真 RTL 落盘；随后
    // outer_product_mul.json 再把这些 RTL 文件加入 HLS black-box。
    s"""set ip_dir [file normalize ${tclString(ipDir)}]
      |file mkdir $$ip_dir
      |create_project -in_memory -part xcu55c-fsvh2892-2L-e
      |
      |# 生成 Xilinx Floating Point IP：FP64 multiply，latency=6。
      |# Chisel wrapper 只实例化 outer_product_mul_dmul_ip 这个模块名；
      |# 真正 IP RTL 由这个 Tcl 在 make chisel / make xo 前生成。
      |set vivado_ver [version -short]
      |set fpo_ver 7.1
      |if {[regexp -nocase {2015\\.1.*} $$vivado_ver match]} {
      |    set fpo_ver 7.0
      |}
      |
      |create_ip -name floating_point -version $$fpo_ver -vendor xilinx.com -library ip -module_name outer_product_mul_dmul_ip -dir $$ip_dir -force
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
      |                          CONFIG.component_name outer_product_mul_dmul_ip \\
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
      |                          CONFIG.result_tlast_behv Null] [get_ips outer_product_mul_dmul_ip] -quiet
      |set_property generate_synth_checkpoint false [get_files outer_product_mul_dmul_ip.xci]
      |generate_target {synthesis simulation} [get_files outer_product_mul_dmul_ip.xci]
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
