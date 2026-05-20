#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

#include "ert.h"
#include "experimental/xrt_bo.h"
#include "experimental/xrt_device.h"
#include "experimental/xrt_kernel.h"
#include "hls_pipeline_demo.hpp"

namespace {

constexpr double kDefaultKernelMHz = 300.300293;
constexpr int kTopPipelineItems = PROJECTX_HLS_TOP_PIPELINE_DEMO_ITEMS;

// host 命令行暴露测试规模、设备编号，以及可选的计时参数。
struct HostOptions {
    unsigned int device_index = 0;
    int item_count = 32;
    bool timing = false;
    bool device_timing = true;
    double kernel_mhz = kDefaultKernelMHz;
    unsigned int repeat = 1;
    unsigned int warmup = 0;
};

// CPU golden 的三组结果。
// 它们和 kernel 的三个输出数组一一对应，用来检查流水线内部观测点
// 和最终输出是否都符合预期。
struct ExpectedValues {
    std::vector<int> stage_value;
    std::vector<int> delayed_value;
    std::vector<int> output_value;
};

struct DemoBuffers {
    std::vector<int> output;
    std::vector<int> stage_value;
    std::vector<int> delayed_value;
    int* output_mapped = nullptr;
    int* stage_mapped = nullptr;
    int* delayed_mapped = nullptr;
    xrt::bo output_bo;
    xrt::bo stage_bo;
    xrt::bo delayed_bo;
};

struct DeviceKernelTiming {
    bool valid = false;
    double duration_ms = 0.0;
    double duration_cycles = 0.0;
};

using Clock = std::chrono::steady_clock;

double elapsed_ms(Clock::time_point begin, Clock::time_point end) {
    return std::chrono::duration<double, std::milli>(end - begin).count();
}

// 解析浮点数，并拒绝带尾随字符的输入。
double parse_double(const char* text, const char* name) {
    char* end = nullptr;
    const double value = std::strtod(text, &end);
    if (end == text || *end != '\0') {
        throw std::runtime_error(std::string("invalid ") + name + ": " + text);
    }
    return value;
}

// 解析无符号整数，并拒绝带尾随字符的输入。
unsigned int parse_uint(const char* text, const char* name) {
    char* end = nullptr;
    const unsigned long value = std::strtoul(text, &end, 10);
    if (end == text || *end != '\0') {
        throw std::runtime_error(std::string("invalid ") + name + ": " + text);
    }
    return static_cast<unsigned int>(value);
}

unsigned int parse_positive_uint(const char* text, const char* name) {
    const unsigned int value = parse_uint(text, name);
    if (value == 0) {
        throw std::runtime_error(std::string(name) + " must be >= 1");
    }
    return value;
}

// item_count 不能超过 HLS kernel 里 scratch BRAM 的固定上限。
int parse_item_count(const char* text) {
    const unsigned int value = parse_uint(text, "item_count");
    if (value == 0 || value > PROJECTX_HLS_PIPELINE_DEMO_MAX_ITEMS) {
        throw std::runtime_error("item_count must be in [1, 4096]");
    }
    return static_cast<int>(value);
}

// 命令行格式：
//   host.exe <xclbin> [item_count] [device_index] [--timing ...]
HostOptions parse_options(int argc, char** argv) {
    HostOptions options;
    int arg = 2;

    if (arg < argc && argv[arg][0] != '-') {
        options.item_count = parse_item_count(argv[arg]);
        ++arg;
    }
    if (arg < argc && argv[arg][0] != '-') {
        options.device_index = parse_uint(argv[arg], "device_index");
        ++arg;
    }

    for (; arg < argc; ++arg) {
        const std::string current = argv[arg];
        if (current == "--timing") {
            options.timing = true;
        } else if (current == "--repeat") {
            if (++arg >= argc) {
                throw std::runtime_error("--repeat requires a value");
            }
            options.repeat = parse_positive_uint(argv[arg], "repeat");
            options.timing = true;
        } else if (current == "--warmup") {
            if (++arg >= argc) {
                throw std::runtime_error("--warmup requires a value");
            }
            options.warmup = parse_uint(argv[arg], "warmup");
            options.timing = true;
        } else if (current == "--kernel-mhz") {
            if (++arg >= argc) {
                throw std::runtime_error("--kernel-mhz requires a value");
            }
            options.kernel_mhz = parse_double(argv[arg], "kernel_mhz");
            if (options.kernel_mhz <= 0.0) {
                throw std::runtime_error("kernel_mhz must be > 0");
            }
            options.timing = true;
        } else if (current == "--no-device-timing") {
            options.device_timing = false;
        } else {
            throw std::runtime_error("unknown option: " + current);
        }
    }
    return options;
}

void usage(const char* argv0) {
    std::cerr << "用法：\n"
              << "  " << argv0 << " <xclbin> [item_count] [device_index] [--timing] [--warmup N] [--repeat N]\n\n"
              << "这个 host 会依次启动三组 demo：\n"
              << "  1. krnl_hls_pipeline_demo 单 kernel 内部 DATAFLOW 流水\n"
              << "  2. krnl_hls_top_pipeline_demo 顶层函数级 PIPELINE 流水\n"
              << "  3. source -> compute -> sink 三 kernel 之间 AXI4-Stream 流水\n\n"
              << "三组 demo 都检查三组数组：\n"
              << "  stage_value   compute 流水段当前拍产生的值\n"
              << "  delayed_value 两级寄存器延迟链暴露出的历史值\n"
              << "  output        stage_value + delayed_value\n\n"
              << "计时选项：\n"
              << "  --timing      打印 host 侧分段耗时和 kernel 启动到完成耗时\n"
              << "  --warmup N    正式计时前先运行 N 次三组 demo\n"
              << "  --repeat N    正式计时运行 N 次三组 demo，输出 min/avg/max\n"
              << "  --kernel-mhz  设备侧时间换算周期时使用的频率，默认 300.300293 MHz\n"
              << "  --no-device-timing  关闭 XRT/ERT 设备侧 kernel 时间戳输出\n";
}

// 构造一个确定性输入，避免 demo 依赖外部数据文件。
// 注意：input[i] = 3*i+1 是 host/CPU 侧准备测试向量的公式，
// FPGA 不负责生成这个输入序列，只负责读取它并执行后面的流水计算。
std::vector<int> build_input(int item_count) {
    std::vector<int> input(static_cast<std::size_t>(item_count));
    for (int index = 0; index < item_count; ++index) {
        input[index] = 3 * index + 1;
    }
    return input;
}

// CPU 侧 golden model。
// 这里复刻 FPGA compute stage 和两级寄存器延迟链的行为：
//   mixed = projectx_hls_pipeline_demo_mix(input[i], i)
//   delayed = 两拍前的 mixed，前两拍为初始 0
//   output = mixed + delayed
// 注意 delay0/delay1 的更新顺序必须和 HLS kernel 保持一致：
// delayed 先取旧的 delay1，然后才推进寄存器链。
ExpectedValues build_expected(const std::vector<int>& input) {
    ExpectedValues expected;
    expected.stage_value.resize(input.size());
    expected.delayed_value.resize(input.size());
    expected.output_value.resize(input.size());

    int delay0 = 0;
    int delay1 = 0;
    for (std::size_t index = 0; index < input.size(); ++index) {
        const int mixed = projectx_hls_pipeline_demo_mix(input[index], static_cast<int>(index));
        const int delayed = delay1;
        delay1 = delay0;
        delay0 = mixed;

        expected.stage_value[index] = mixed;
        expected.delayed_value[index] = delayed;
        expected.output_value[index] = mixed + delayed;
    }

    return expected;
}

template <typename T>
xrt::bo make_input_bo(xrt::device& device, xrt::kernel& kernel, int arg_index, const std::vector<T>& data) {
    // group_id(arg_index) 让 XRT 根据 kernel 参数所在的 memory group 分配 BO。
    // arg_index 必须和 HLS 函数签名顺序一致。
    xrt::bo bo(device, data.size() * sizeof(T), kernel.group_id(arg_index));
    auto mapped = bo.map<T*>();
    std::copy(data.begin(), data.end(), mapped);
    bo.sync(XCL_BO_SYNC_BO_TO_DEVICE, data.size() * sizeof(T), 0);
    return bo;
}

template <typename T>
xrt::bo make_output_bo(xrt::device& device,
                       xrt::kernel& kernel,
                       int arg_index,
                       std::vector<T>& storage,
                       T*& mapped) {
    // 输出 BO 保留 map 出来的指针，kernel 结束并 FROM_DEVICE sync 后，
    // host 可以直接通过 mapped[index] 读取回写结果。
    xrt::bo bo(device, storage.size() * sizeof(T), kernel.group_id(arg_index));
    mapped = bo.map<T*>();
    std::fill(mapped, mapped + storage.size(), T{});
    bo.sync(XCL_BO_SYNC_BO_TO_DEVICE, storage.size() * sizeof(T), 0);
    return bo;
}

bool check_array(const char* name, const int* actual, const std::vector<int>& expected) {
    // 逐项比较并只打印前几处错误，避免大规模测试时刷屏。
    bool ok = true;
    for (std::size_t index = 0; index < expected.size(); ++index) {
        if (actual[index] != expected[index]) {
            if (ok) {
                std::cerr << "Mismatch in " << name << ":\n";
            }
            ok = false;
            std::cerr << "  [" << index << "] fpga=" << actual[index]
                      << " expected=" << expected[index] << "\n";
            if (index > 16) {
                std::cerr << "  ...\n";
                break;
            }
        }
    }
    return ok;
}

DemoBuffers make_demo_buffers(xrt::device& device,
                              xrt::kernel& output_kernel,
                              int output_arg_index,
                              xrt::kernel& stage_kernel,
                              int stage_arg_index,
                              int delayed_arg_index,
                              int item_count) {
    DemoBuffers buffers;
    buffers.output.assign(static_cast<std::size_t>(item_count), 0);
    buffers.stage_value.assign(static_cast<std::size_t>(item_count), 0);
    buffers.delayed_value.assign(static_cast<std::size_t>(item_count), 0);

    buffers.output_bo =
        make_output_bo(device, output_kernel, output_arg_index, buffers.output, buffers.output_mapped);
    buffers.stage_bo =
        make_output_bo(device, stage_kernel, stage_arg_index, buffers.stage_value, buffers.stage_mapped);
    buffers.delayed_bo =
        make_output_bo(device, stage_kernel, delayed_arg_index, buffers.delayed_value, buffers.delayed_mapped);
    return buffers;
}

void sync_outputs_from_device(DemoBuffers& buffers) {
    buffers.output_bo.sync(XCL_BO_SYNC_BO_FROM_DEVICE, buffers.output.size() * sizeof(int), 0);
    buffers.stage_bo.sync(XCL_BO_SYNC_BO_FROM_DEVICE, buffers.stage_value.size() * sizeof(int), 0);
    buffers.delayed_bo.sync(XCL_BO_SYNC_BO_FROM_DEVICE, buffers.delayed_value.size() * sizeof(int), 0);
}

bool check_demo_outputs(const char* title, const DemoBuffers& buffers, const ExpectedValues& expected) {
    std::cout << title << " first values:\n";
    const int print_count = static_cast<int>(std::min<std::size_t>(buffers.output.size(), 8));
    for (int index = 0; index < print_count; ++index) {
        std::cout << "  i=" << std::setw(2) << index
                  << " stage=" << std::setw(4) << buffers.stage_mapped[index]
                  << " delay=" << std::setw(4) << buffers.delayed_mapped[index]
                  << " out=" << std::setw(4) << buffers.output_mapped[index] << "\n";
    }
    if (buffers.output.size() > static_cast<std::size_t>(print_count)) {
        std::cout << "  ...\n";
    }

    return check_array("stage_value", buffers.stage_mapped, expected.stage_value) &&
           check_array("delayed_value", buffers.delayed_mapped, expected.delayed_value) &&
           check_array("output", buffers.output_mapped, expected.output_value);
}

void configure_single_run(xrt::run& run,
                          int item_count,
                          xrt::bo& input_bo,
                          DemoBuffers& buffers) {
    run.set_arg(0, item_count);
    run.set_arg(1, input_bo);
    run.set_arg(2, buffers.output_bo);
    run.set_arg(3, buffers.stage_bo);
    run.set_arg(4, buffers.delayed_bo);
}

void configure_top_pipeline_run(xrt::run& run, xrt::bo& input_bo, DemoBuffers& buffers) {
    run.set_arg(0, input_bo);
    run.set_arg(1, buffers.output_bo);
    run.set_arg(2, buffers.stage_bo);
    run.set_arg(3, buffers.delayed_bo);
}

bool enable_device_kernel_timestamps(xrt::run& run) {
    if (auto pkt = run.get_ert_packet()) {
        auto* skcmd = to_start_krnl_pkg(pkt);
        skcmd->stat_enabled = 1;
        return true;
    }
    return false;
}

DeviceKernelTiming read_device_kernel_timing(const xrt::run& run, double kernel_mhz) {
    DeviceKernelTiming timing;
    auto* pkt = run.get_ert_packet();
    if (!pkt) {
        return timing;
    }

    auto* skcmd = to_start_krnl_pkg(pkt);
    if (!skcmd->stat_enabled) {
        return timing;
    }

    auto* timestamps = ert_start_kernel_timestamps(skcmd);
    const uint64_t running_ns = timestamps->skc_timestamps[ERT_CMD_STATE_RUNNING];
    const uint64_t completed_ns = timestamps->skc_timestamps[ERT_CMD_STATE_COMPLETED];
    if (running_ns == 0 || completed_ns == 0 || completed_ns < running_ns) {
        return timing;
    }

    const uint64_t duration_ns = completed_ns - running_ns;
    timing.valid = true;
    timing.duration_ms = static_cast<double>(duration_ns) / 1.0e6;
    timing.duration_cycles = (static_cast<double>(duration_ns) * kernel_mhz) / 1000.0;
    return timing;
}

void print_summary(const char* name, const std::vector<double>& values, const char* unit) {
    if (values.empty()) {
        std::cout << "  " << name << "_" << unit << "=unavailable\n";
        return;
    }

    const auto minmax = std::minmax_element(values.begin(), values.end());
    double sum = 0.0;
    for (double value : values) {
        sum += value;
    }
    const double avg = sum / static_cast<double>(values.size());

    std::cout << "  " << name << "_min_" << unit << "=" << *minmax.first << "\n"
              << "  " << name << "_avg_" << unit << "=" << avg << "\n"
              << "  " << name << "_max_" << unit << "=" << *minmax.second << "\n";
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        usage(argv[0]);
        return 2;
    }

    try {
        const auto total_start = Clock::now();
        const std::string xclbin_path = argv[1];
        const HostOptions options = parse_options(argc, argv);

        const auto prepare_start = Clock::now();
        const std::vector<int> input = build_input(options.item_count);
        const std::vector<int> top_pipeline_input = build_input(kTopPipelineItems);
        // 先在 host 侧计算 golden，再启动 FPGA/sw_emu kernel。
        const ExpectedValues expected = build_expected(input);
        const ExpectedValues top_pipeline_expected = build_expected(top_pipeline_input);
        const auto prepare_end = Clock::now();

        const auto xrt_setup_start = Clock::now();
        std::cout << "Opening device " << options.device_index << "\n";
        auto device = xrt::device(options.device_index);

        std::cout << "Loading xclbin: " << xclbin_path << "\n";
        auto uuid = device.load_xclbin(xclbin_path);
        auto single_kernel = xrt::kernel(device, uuid.get(), "krnl_hls_pipeline_demo");
        auto top_pipeline_kernel = xrt::kernel(device, uuid.get(), "krnl_hls_top_pipeline_demo");
        auto source_kernel = xrt::kernel(device, uuid.get(), "krnl_hls_pipeline_source");
        auto compute_kernel = xrt::kernel(device, uuid.get(), "krnl_hls_pipeline_compute");
        auto sink_kernel = xrt::kernel(device, uuid.get(), "krnl_hls_pipeline_sink");
        const auto xrt_setup_end = Clock::now();

        // 单 kernel 版本签名是：
        //   arg0 item_count
        //   arg1 input
        //   arg2 output
        //   arg3 stage_value
        //   arg4 delayed_value
        // 下面的 BO 分配必须使用相同参数序号。
        const auto single_buffer_start = Clock::now();
        auto single_input_bo = make_input_bo(device, single_kernel, 1, input);
        DemoBuffers single_buffers =
            make_demo_buffers(device, single_kernel, 2, single_kernel, 3, 4, options.item_count);
        const auto single_buffer_end = Clock::now();

        std::cout << "Running single-kernel DATAFLOW demo(item_count=" << options.item_count << ")\n";
        if (options.warmup > 0 || options.repeat > 1) {
            std::cout << "  warmup=" << options.warmup << " repeat=" << options.repeat << "\n";
        }

        for (unsigned int iter = 0; iter < options.warmup; ++iter) {
            xrt::run run(single_kernel);
            configure_single_run(run, options.item_count, single_input_bo, single_buffers);
            run.start();
            run.wait();
        }

        std::vector<double> single_host_ms;
        std::vector<double> single_device_ms;
        std::vector<double> single_device_cycles;
        single_host_ms.reserve(options.repeat);
        single_device_ms.reserve(options.repeat);
        single_device_cycles.reserve(options.repeat);
        for (unsigned int iter = 0; iter < options.repeat; ++iter) {
            xrt::run run(single_kernel);
            configure_single_run(run, options.item_count, single_input_bo, single_buffers);
            const bool device_timing_enabled =
                options.timing && options.device_timing && enable_device_kernel_timestamps(run);
            const auto kernel_start = Clock::now();
            run.start();
            run.wait();
            const auto kernel_end = Clock::now();
            single_host_ms.push_back(elapsed_ms(kernel_start, kernel_end));
            if (device_timing_enabled) {
                const auto timing = read_device_kernel_timing(run, options.kernel_mhz);
                if (timing.valid) {
                    single_device_ms.push_back(timing.duration_ms);
                    single_device_cycles.push_back(timing.duration_cycles);
                }
            }
        }

        // kernel 完成后，把三个输出数组从 device 同步回 host 可见内存。
        const auto single_d2h_start = Clock::now();
        sync_outputs_from_device(single_buffers);
        const auto single_d2h_end = Clock::now();

        // 顶层函数级 PIPELINE 版本签名是：
        //   arg0 input
        //   arg1 output
        //   arg2 stage_value
        //   arg3 delayed_value
        // 它处理固定 4 元素 micro-batch，用来让 HLS top schedule 本身报告 Pipeline=1。
        const auto top_pipeline_buffer_start = Clock::now();
        auto top_pipeline_input_bo = make_input_bo(device, top_pipeline_kernel, 0, top_pipeline_input);
        DemoBuffers top_pipeline_buffers = make_demo_buffers(
            device, top_pipeline_kernel, 1, top_pipeline_kernel, 2, 3, kTopPipelineItems);
        const auto top_pipeline_buffer_end = Clock::now();

        std::cout << "Running top-level PIPELINE demo(item_count=" << kTopPipelineItems << ")\n";
        if (options.warmup > 0 || options.repeat > 1) {
            std::cout << "  warmup=" << options.warmup << " repeat=" << options.repeat << "\n";
        }

        for (unsigned int iter = 0; iter < options.warmup; ++iter) {
            xrt::run run(top_pipeline_kernel);
            configure_top_pipeline_run(run, top_pipeline_input_bo, top_pipeline_buffers);
            run.start();
            run.wait();
        }

        std::vector<double> top_pipeline_host_ms;
        std::vector<double> top_pipeline_device_ms;
        std::vector<double> top_pipeline_device_cycles;
        top_pipeline_host_ms.reserve(options.repeat);
        top_pipeline_device_ms.reserve(options.repeat);
        top_pipeline_device_cycles.reserve(options.repeat);
        for (unsigned int iter = 0; iter < options.repeat; ++iter) {
            xrt::run run(top_pipeline_kernel);
            configure_top_pipeline_run(run, top_pipeline_input_bo, top_pipeline_buffers);
            const bool device_timing_enabled =
                options.timing && options.device_timing && enable_device_kernel_timestamps(run);
            const auto kernel_start = Clock::now();
            run.start();
            run.wait();
            const auto kernel_end = Clock::now();
            top_pipeline_host_ms.push_back(elapsed_ms(kernel_start, kernel_end));
            if (device_timing_enabled) {
                const auto timing = read_device_kernel_timing(run, options.kernel_mhz);
                if (timing.valid) {
                    top_pipeline_device_ms.push_back(timing.duration_ms);
                    top_pipeline_device_cycles.push_back(timing.duration_cycles);
                }
            }
        }

        const auto top_pipeline_d2h_start = Clock::now();
        sync_outputs_from_device(top_pipeline_buffers);
        const auto top_pipeline_d2h_end = Clock::now();

        // 三 kernel 版本签名是：
        //   source:  arg0 input,        arg1 output_stream, arg2 item_count
        //   compute: arg0 input_stream, arg1 output_stream, arg2 stage_value,
        //            arg3 delayed_value, arg4 item_count
        //   sink:    arg0 input_stream, arg1 output,        arg2 item_count
        //
        // stream 端口由 xclbin 内的 stream_connect 连接，host 不设置 stream arg。
        const auto stream_buffer_start = Clock::now();
        auto stream_input_bo = make_input_bo(device, source_kernel, 0, input);
        DemoBuffers stream_buffers =
            make_demo_buffers(device, sink_kernel, 1, compute_kernel, 2, 3, options.item_count);
        const auto stream_buffer_end = Clock::now();

        std::vector<double> stream_host_ms;
        std::vector<double> source_device_ms;
        std::vector<double> source_device_cycles;
        std::vector<double> compute_device_ms;
        std::vector<double> compute_device_cycles;
        std::vector<double> sink_device_ms;
        std::vector<double> sink_device_cycles;
        stream_host_ms.reserve(options.repeat);
        source_device_ms.reserve(options.repeat);
        source_device_cycles.reserve(options.repeat);
        compute_device_ms.reserve(options.repeat);
        compute_device_cycles.reserve(options.repeat);
        sink_device_ms.reserve(options.repeat);
        sink_device_cycles.reserve(options.repeat);

        auto configure_stream_runs = [&]() {
            xrt::run sink_run(sink_kernel);
            sink_run.set_arg(1, stream_buffers.output_bo);
            sink_run.set_arg(2, options.item_count);

            xrt::run compute_run(compute_kernel);
            compute_run.set_arg(2, stream_buffers.stage_bo);
            compute_run.set_arg(3, stream_buffers.delayed_bo);
            compute_run.set_arg(4, options.item_count);

            xrt::run source_run(source_kernel);
            source_run.set_arg(0, stream_input_bo);
            source_run.set_arg(2, options.item_count);

            return std::make_tuple(std::move(source_run), std::move(compute_run), std::move(sink_run));
        };

        // 启动顺序用 sink -> compute -> source。
        // 下游先启动可以避免上游 stream 写入时因为没人消费而阻塞。
        std::cout << "Running kernel-to-kernel stream demo(item_count=" << options.item_count << ")\n";
        if (options.warmup > 0 || options.repeat > 1) {
            std::cout << "  warmup=" << options.warmup << " repeat=" << options.repeat << "\n";
        }

        for (unsigned int iter = 0; iter < options.warmup; ++iter) {
            auto [source_run, compute_run, sink_run] = configure_stream_runs();
            sink_run.start();
            compute_run.start();
            source_run.start();
            source_run.wait();
            compute_run.wait();
            sink_run.wait();
        }

        for (unsigned int iter = 0; iter < options.repeat; ++iter) {
            auto [source_run, compute_run, sink_run] = configure_stream_runs();
            const bool source_device_timing_enabled =
                options.timing && options.device_timing && enable_device_kernel_timestamps(source_run);
            const bool compute_device_timing_enabled =
                options.timing && options.device_timing && enable_device_kernel_timestamps(compute_run);
            const bool sink_device_timing_enabled =
                options.timing && options.device_timing && enable_device_kernel_timestamps(sink_run);

            const auto stream_start = Clock::now();
            sink_run.start();
            compute_run.start();
            source_run.start();
            source_run.wait();
            compute_run.wait();
            sink_run.wait();
            const auto stream_end = Clock::now();
            stream_host_ms.push_back(elapsed_ms(stream_start, stream_end));

            if (source_device_timing_enabled) {
                const auto timing = read_device_kernel_timing(source_run, options.kernel_mhz);
                if (timing.valid) {
                    source_device_ms.push_back(timing.duration_ms);
                    source_device_cycles.push_back(timing.duration_cycles);
                }
            }
            if (compute_device_timing_enabled) {
                const auto timing = read_device_kernel_timing(compute_run, options.kernel_mhz);
                if (timing.valid) {
                    compute_device_ms.push_back(timing.duration_ms);
                    compute_device_cycles.push_back(timing.duration_cycles);
                }
            }
            if (sink_device_timing_enabled) {
                const auto timing = read_device_kernel_timing(sink_run, options.kernel_mhz);
                if (timing.valid) {
                    sink_device_ms.push_back(timing.duration_ms);
                    sink_device_cycles.push_back(timing.duration_cycles);
                }
            }
        }

        const auto stream_d2h_start = Clock::now();
        sync_outputs_from_device(stream_buffers);
        const auto stream_d2h_end = Clock::now();

        // 同时检查中间观测点和最终输出。
        // 这比只检查 output 更适合后续验证 XS 等价体的流水线阶段行为。
        const auto verify_start = Clock::now();
        std::cout << "Input first values:\n";
        const int input_print_count = std::min(options.item_count, 8);
        for (int index = 0; index < input_print_count; ++index) {
            std::cout << "  i=" << std::setw(2) << index
                      << " in=" << std::setw(3) << input[index] << "\n";
        }
        if (options.item_count > input_print_count) {
            std::cout << "  ...\n";
        }

        const bool ok = check_demo_outputs("single-kernel", single_buffers, expected) &&
                        check_demo_outputs(
                            "top-level-pipeline", top_pipeline_buffers, top_pipeline_expected) &&
                        check_demo_outputs("kernel-to-kernel", stream_buffers, expected);
        if (!ok) {
            std::cerr << "ERROR: result mismatch\n";
            return 1;
        }
        const auto verify_end = Clock::now();

        if (options.timing) {
            const auto total_end = Clock::now();
            std::cout << "Timing ms:\n"
                      << "  prepare_cpu=" << elapsed_ms(prepare_start, prepare_end) << "\n"
                      << "  xrt_setup=" << elapsed_ms(xrt_setup_start, xrt_setup_end) << "\n"
                      << "  single_buffer_h2d=" << elapsed_ms(single_buffer_start, single_buffer_end) << "\n";
            print_summary("single_kernel_host", single_host_ms, "ms");
            std::cout << "  single_buffer_d2h=" << elapsed_ms(single_d2h_start, single_d2h_end) << "\n"
                      << "  top_pipeline_buffer_h2d="
                      << elapsed_ms(top_pipeline_buffer_start, top_pipeline_buffer_end) << "\n";
            print_summary("top_pipeline_kernel_host", top_pipeline_host_ms, "ms");
            std::cout << "  top_pipeline_buffer_d2h="
                      << elapsed_ms(top_pipeline_d2h_start, top_pipeline_d2h_end) << "\n"
                      << "  stream_buffer_h2d=" << elapsed_ms(stream_buffer_start, stream_buffer_end) << "\n";
            print_summary("stream_pipeline_host", stream_host_ms, "ms");
            std::cout << "  stream_buffer_d2h=" << elapsed_ms(stream_d2h_start, stream_d2h_end) << "\n"
                      << "  verify=" << elapsed_ms(verify_start, verify_end) << "\n"
                      << "  total=" << elapsed_ms(total_start, total_end) << "\n";

            if (options.device_timing) {
                std::cout << "Device timing:\n"
                          << "  device_kernel_mhz=" << options.kernel_mhz << "\n";
                print_summary("single_kernel_device", single_device_ms, "ms");
                print_summary("single_kernel_device", single_device_cycles, "cycles");
                print_summary("top_pipeline_kernel_device", top_pipeline_device_ms, "ms");
                print_summary("top_pipeline_kernel_device", top_pipeline_device_cycles, "cycles");
                print_summary("source_kernel_device", source_device_ms, "ms");
                print_summary("source_kernel_device", source_device_cycles, "cycles");
                print_summary("compute_kernel_device", compute_device_ms, "ms");
                print_summary("compute_kernel_device", compute_device_cycles, "cycles");
                print_summary("sink_kernel_device", sink_device_ms, "ms");
                print_summary("sink_kernel_device", sink_device_cycles, "cycles");
            }
        }

        std::cout << "PASS\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "ERROR: " << error.what() << "\n";
        return 1;
    }
}
