#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <exception>
#include <iomanip>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "experimental/xrt_bo.h"
#include "experimental/xrt_device.h"
#include "experimental/xrt_kernel.h"

namespace {

constexpr int kSlotsPerRow = 3;
constexpr int kMaxRowsForOuterProduct = 512;

struct HostOptions {
    unsigned int device_index = 0;
    bool timing = false;
    unsigned int repeat = 1;
    unsigned int warmup = 0;
};

using Clock = std::chrono::steady_clock;

double elapsed_ms(Clock::time_point begin, Clock::time_point end) {
    return std::chrono::duration<double, std::milli>(end - begin).count();
}

// 解析浮点命令行参数，并要求整串文本都能合法转换。
double parse_double(const char* text, const char* name) {
    char* end = nullptr;
    const double value = std::strtod(text, &end);
    if (end == text || *end != '\0') {
        throw std::runtime_error(std::string("invalid ") + name + ": " + text);
    }
    return value;
}

// 解析无符号整数参数，拒绝带尾随垃圾字符的输入。
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

// 让 demo 的规模保持在 kernel 当前假设的固定上限内。
int parse_rows(const char* text) {
    const unsigned int rows = parse_uint(text, "rows");
    if (rows == 0 || rows > kMaxRowsForOuterProduct) {
        throw std::runtime_error("rows must be in [1, 512] when yy_t output is enabled");
    }
    return static_cast<int>(rows);
}

HostOptions parse_options(int argc, char** argv) {
    HostOptions options;
    bool device_index_seen = false;

    for (int arg = 5; arg < argc; ++arg) {
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
        } else if (!current.empty() && current[0] != '-' && !device_index_seen) {
            options.device_index = parse_uint(argv[arg], "device_index");
            device_index_seen = true;
        } else {
            throw std::runtime_error("unknown option: " + current);
        }
    }

    return options;
}

void usage(const char* argv0) {
    std::cerr << "用法：\n"
              << "  " << argv0 << " <xclbin> <rows> <scale> <x0> [device_index] [--timing] [--warmup N] [--repeat N]\n\n"
              << "host 会生成一个 ELLPACK 格式的三对角稀疏矩阵，\n"
              << "设置 x[i] = x0 + i，然后让 FPGA 计算 y = scale * A * x，\n"
              << "并输出 yy_t = y * y^T。\n\n"
              << "计时选项：\n"
              << "  --timing      打印 host 侧分段耗时\n"
              << "  --warmup N    正式计时前先运行 N 次 kernel\n"
              << "  --repeat N    正式计时运行 N 次 kernel，输出 min/avg/max\n";
}

// 生成一个很小的 ELLPACK 风格三对角矩阵，让 kernel 输入完全可控，
// 方便从 host 侧观察和验证。
void build_tridiagonal_matrix(int rows, std::vector<int>& col_idx, std::vector<double>& values) {
    col_idx.assign(rows * kSlotsPerRow, -1);
    values.assign(rows * kSlotsPerRow, 0.0);

    for (int row = 0; row < rows; ++row) {
        const int base = row * kSlotsPerRow;

        if (row > 0) {
            col_idx[base + 0] = row - 1;
            values[base + 0] = -1.0;
        }

        col_idx[base + 1] = row;
        values[base + 1] = 4.0;

        if (row + 1 < rows) {
            col_idx[base + 2] = row + 1;
            values[base + 2] = -1.0;
        }
    }
}

// CPU 侧 golden model，用来在 FPGA 回读后做结果校验。
std::vector<double> cpu_spmv(int rows,
                             double scale,
                             const std::vector<int>& col_idx,
                             const std::vector<double>& values,
                             const std::vector<double>& x) {
    std::vector<double> y(rows, 0.0);

    for (int row = 0; row < rows; ++row) {
        double acc = 0.0;
        for (int slot = 0; slot < kSlotsPerRow; ++slot) {
            const int idx = row * kSlotsPerRow + slot;
            const int col = col_idx[idx];
            if (col >= 0) {
                acc += values[idx] * x[col];
            }
        }
        y[row] = scale * acc;
    }

    return y;
}

std::vector<double> cpu_outer_product(const std::vector<double>& y) {
    const size_t rows = y.size();
    std::vector<double> yy_t(rows * rows, 0.0);

    for (size_t row = 0; row < rows; ++row) {
        for (size_t col = 0; col < rows; ++col) {
            yy_t[row * rows + col] = y[row] * y[col];
        }
    }

    return yy_t;
}

// 这里用函数模板复用“分配 BO / 填充 / sync”这套流程。
// 如果想看 template <typename T>、sizeof(T)、bo.map<T*>() 的详细解释，
// 见：~/ProjectFS/Project-X/docs/C++模板与make_bo详解_zh.md
template <typename T>
xrt::bo make_bo(xrt::device& device, xrt::kernel& kernel, int arg_index, const std::vector<T>& data) {
    // 为第 arg_index 个 kernel 参数分配 BO。
    // 这个参数最终落到哪个 HBM bank，不是在这里写死的，
    // 而是由当前 VARIANT 选中的 hardware/krnl_spmv_*.cpp 里的端口名
    // + cfg/u55c.cfg 的 connectivity 共同决定。
    xrt::bo bo(device, data.size() * sizeof(T), kernel.group_id(arg_index));
    auto mapped = bo.map<T*>();
    std::copy(data.begin(), data.end(), mapped);
    // 先写入 host 可见映射区，再同步到 device 侧 BO。
    bo.sync(XCL_BO_SYNC_BO_TO_DEVICE, data.size() * sizeof(T), 0);
    return bo;
}

} // 匿名 namespace

int main(int argc, char** argv) {
    if (argc < 5) {
        usage(argv[0]);
        return 2;
    }

    try {
        const auto total_start = Clock::now();

        // 命令行同时决定 xclbin 路径和这次要跑的一个小型确定性测试用例。
        const std::string xclbin_path = argv[1];
        const int rows = parse_rows(argv[2]);
        const double scale = parse_double(argv[3], "scale");
        const double x0 = parse_double(argv[4], "x0");
        const HostOptions options = parse_options(argc, argv);

        const auto prepare_start = Clock::now();
        std::vector<int> col_idx;
        std::vector<double> values;
        std::vector<double> x(rows);
        std::vector<double> y(rows, 0.0);
        std::vector<double> yy_t(static_cast<size_t>(rows) * static_cast<size_t>(rows), 0.0);

        // 这个 demo 的稀疏矩阵和输入向量都由 host 现场生成，
        // 不依赖外部数据文件。
        build_tridiagonal_matrix(rows, col_idx, values);
        for (int i = 0; i < rows; ++i) {
            x[i] = x0 + static_cast<double>(i);
        }

        // 先在 CPU 上算一遍同样的 SpMV，作为后面 FPGA 结果的对照。
        const std::vector<double> expected = cpu_spmv(rows, scale, col_idx, values, x);
        const std::vector<double> expected_outer = cpu_outer_product(expected);
        const auto prepare_end = Clock::now();

        const auto xrt_setup_start = Clock::now();
        std::cout << "Opening device " << options.device_index << "\n";
        auto device = xrt::device(options.device_index);

        std::cout << "Loading xclbin: " << xclbin_path << "\n";
        auto uuid = device.load_xclbin(xclbin_path);

        auto kernel = xrt::kernel(device, uuid.get(), "krnl_spmv");
        const auto xrt_setup_end = Clock::now();

        // kernel 参数顺序定义在当前 VARIANT 选中的 hardware/krnl_spmv_*.cpp
        // 的函数签名里：
        //   void krnl_spmv(int num_rows,
        //                  double scale,
        //                  const int* col_idx,
        //                  const double* values,
        //                  const double* x,
        //                  double* y,
        //                  double* yy_t)
        //
        // 因此 XRT 看到的参数序号就是：
        //   arg0 rows, arg1 scale, arg2 col_idx, arg3 values, arg4 x, arg5 y, arg6 yy_t
        //
        // 下面 make_bo(..., 2/3/4, ...) 里的 2/3/4，
        // 就是在引用这个函数签名中的第 3/4/5 个参数。
        // cfg/u55c.cfg 只负责把这些参数名对应的 m_axi 端口映射到 HBM bank，
        // 不负责定义参数顺序。
        const auto buffer_h2d_start = Clock::now();
        auto col_idx_bo = make_bo(device, kernel, 2, col_idx);
        auto values_bo = make_bo(device, kernel, 3, values);
        auto x_bo = make_bo(device, kernel, 4, x);

        // y 是输出缓冲区，所以这里没有直接复用 make_bo：
        // make_bo 更适合“把现成输入数组灌进 BO 然后返回 BO”这类场景；
        // 而 y 这边除了分配和初始化 BO，还要把映射指针 y_mapped 保留下来，
        // 这样 kernel 运行结束并执行 FROM_DEVICE sync 之后，
        // host 可以直接通过 y_mapped[i] 读取结果做校验。
        //
        // 这里仍然先把 y 的初值同步到 device，
        // 是为了让 sw_emu 和真实硬件走同一套输入/输出缓冲路径。
        xrt::bo y_bo(device, y.size() * sizeof(double), kernel.group_id(5));
        auto y_mapped = y_bo.map<double*>();
        std::copy(y.begin(), y.end(), y_mapped);
        y_bo.sync(XCL_BO_SYNC_BO_TO_DEVICE, y.size() * sizeof(double), 0);

        xrt::bo yy_t_bo(device, yy_t.size() * sizeof(double), kernel.group_id(6));
        auto yy_t_mapped = yy_t_bo.map<double*>();
        std::copy(yy_t.begin(), yy_t.end(), yy_t_mapped);
        yy_t_bo.sync(XCL_BO_SYNC_BO_TO_DEVICE, yy_t.size() * sizeof(double), 0);
        const auto buffer_h2d_end = Clock::now();

        // 启动 kernel，一次传入标量参数和各个 BO 句柄。
        std::cout << "Running krnl_spmv(rows=" << rows << ", scale=" << scale << ", x0=" << x0 << ")";
        if (options.warmup > 0 || options.repeat > 1) {
            std::cout << " warmup=" << options.warmup << " repeat=" << options.repeat;
        }
        std::cout << "\n";

        for (unsigned int iter = 0; iter < options.warmup; ++iter) {
            auto run = kernel(rows, scale, col_idx_bo, values_bo, x_bo, y_bo, yy_t_bo);
            run.wait();
        }

        std::vector<double> kernel_ms;
        kernel_ms.reserve(options.repeat);
        for (unsigned int iter = 0; iter < options.repeat; ++iter) {
            const auto kernel_start = Clock::now();
            auto run = kernel(rows, scale, col_idx_bo, values_bo, x_bo, y_bo, yy_t_bo);
            run.wait();
            const auto kernel_end = Clock::now();
            kernel_ms.push_back(elapsed_ms(kernel_start, kernel_end));
        }

        // 把输出从 device 拉回 host，再和 CPU golden 做逐项比较。
        const auto d2h_start = Clock::now();
        y_bo.sync(XCL_BO_SYNC_BO_FROM_DEVICE, y.size() * sizeof(double), 0);
        yy_t_bo.sync(XCL_BO_SYNC_BO_FROM_DEVICE, yy_t.size() * sizeof(double), 0);
        const auto d2h_end = Clock::now();

        const auto verify_start = Clock::now();
        double max_abs_diff = 0.0;
        double max_abs_diff_outer = 0.0;
        for (int i = 0; i < rows; ++i) {
            max_abs_diff = std::max(max_abs_diff, std::abs(y_mapped[i] - expected[i]));
        }
        for (size_t i = 0; i < yy_t.size(); ++i) {
            max_abs_diff_outer = std::max(max_abs_diff_outer, std::abs(yy_t_mapped[i] - expected_outer[i]));
        }
        const bool mismatch = (max_abs_diff > 1e-12 || max_abs_diff_outer > 1e-12);
        const auto verify_end = Clock::now();

        std::cout << std::setprecision(17);
        std::cout << "First rows:\n";
        const int rows_to_print = std::min(rows, 8);
        for (int i = 0; i < rows_to_print; ++i) {
            std::cout << "  y[" << i << "] fpga=" << y_mapped[i] << " cpu=" << expected[i] << "\n";
        }
        if (rows > rows_to_print) {
            std::cout << "  ...\n";
        }
        std::cout << "max abs diff: " << max_abs_diff << "\n";
        std::cout << "Outer product first row:\n";
        const int cols_to_print = std::min(rows, 8);
        for (int col = 0; col < cols_to_print; ++col) {
            std::cout << "  yy_t[0," << col << "] fpga=" << yy_t_mapped[col]
                      << " cpu=" << expected_outer[col] << "\n";
        }
        if (rows > cols_to_print) {
            std::cout << "  ...\n";
        }
        std::cout << "max abs diff outer: " << max_abs_diff_outer << "\n";

        if (mismatch) {
            std::cerr << "ERROR: result mismatch\n";
            return 1;
        }

        if (options.timing) {
            const auto minmax = std::minmax_element(kernel_ms.begin(), kernel_ms.end());
            double kernel_sum_ms = 0.0;
            for (double value : kernel_ms) {
                kernel_sum_ms += value;
            }
            const double kernel_avg_ms = kernel_sum_ms / static_cast<double>(kernel_ms.size());
            const auto total_end = Clock::now();

            std::cout << "Timing ms:\n"
                      << "  prepare_cpu=" << elapsed_ms(prepare_start, prepare_end) << "\n"
                      << "  xrt_setup=" << elapsed_ms(xrt_setup_start, xrt_setup_end) << "\n"
                      << "  buffer_h2d=" << elapsed_ms(buffer_h2d_start, buffer_h2d_end) << "\n"
                      << "  kernel_min=" << *minmax.first << "\n"
                      << "  kernel_avg=" << kernel_avg_ms << "\n"
                      << "  kernel_max=" << *minmax.second << "\n"
                      << "  buffer_d2h=" << elapsed_ms(d2h_start, d2h_end) << "\n"
                      << "  verify=" << elapsed_ms(verify_start, verify_end) << "\n"
                      << "  total=" << elapsed_ms(total_start, total_end) << "\n";
        }

        std::cout << "PASS\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << "\n";
        return 1;
    }
}
