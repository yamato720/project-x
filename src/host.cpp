#include <algorithm>
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

double parse_double(const char* text, const char* name) {
    char* end = nullptr;
    const double value = std::strtod(text, &end);
    if (end == text || *end != '\0') {
        throw std::runtime_error(std::string("invalid ") + name + ": " + text);
    }
    return value;
}

unsigned int parse_uint(const char* text, const char* name) {
    char* end = nullptr;
    const unsigned long value = std::strtoul(text, &end, 10);
    if (end == text || *end != '\0') {
        throw std::runtime_error(std::string("invalid ") + name + ": " + text);
    }
    return static_cast<unsigned int>(value);
}

int parse_rows(const char* text) {
    const unsigned int rows = parse_uint(text, "rows");
    if (rows == 0 || rows > 4096) {
        throw std::runtime_error("rows must be in [1, 4096]");
    }
    return static_cast<int>(rows);
}

void usage(const char* argv0) {
    std::cerr << "用法：\n"
              << "  " << argv0 << " <xclbin> <rows> <scale> <x0> [device_index]\n\n"
              << "host 会生成一个 ELLPACK 格式的三对角稀疏矩阵，\n"
              << "设置 x[i] = x0 + i，然后让 FPGA 计算 y = scale * A * x。\n";
}

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

template <typename T>
xrt::bo make_bo(xrt::device& device, xrt::kernel& kernel, int arg_index, const std::vector<T>& data) {
    xrt::bo bo(device, data.size() * sizeof(T), kernel.group_id(arg_index));
    auto mapped = bo.map<T*>();
    std::copy(data.begin(), data.end(), mapped);
    bo.sync(XCL_BO_SYNC_BO_TO_DEVICE, data.size() * sizeof(T), 0);
    return bo;
}

} // 匿名 namespace

int main(int argc, char** argv) {
    if (argc < 5 || argc > 6) {
        usage(argv[0]);
        return 2;
    }

    try {
        const std::string xclbin_path = argv[1];
        const int rows = parse_rows(argv[2]);
        const double scale = parse_double(argv[3], "scale");
        const double x0 = parse_double(argv[4], "x0");
        const unsigned int device_index = (argc == 6) ? parse_uint(argv[5], "device_index") : 0;

        std::vector<int> col_idx;
        std::vector<double> values;
        std::vector<double> x(rows);
        std::vector<double> y(rows, 0.0);

        build_tridiagonal_matrix(rows, col_idx, values);
        for (int i = 0; i < rows; ++i) {
            x[i] = x0 + static_cast<double>(i);
        }

        const std::vector<double> expected = cpu_spmv(rows, scale, col_idx, values, x);

        std::cout << "Opening device " << device_index << "\n";
        auto device = xrt::device(device_index);

        std::cout << "Loading xclbin: " << xclbin_path << "\n";
        auto uuid = device.load_xclbin(xclbin_path);

        auto kernel = xrt::kernel(device, uuid.get(), "krnl_spmv");

        // kernel 参数顺序必须和 krnl_spmv 函数签名一致：
        //   arg0 rows, arg1 scale, arg2 col_idx, arg3 values, arg4 x, arg5 y
        auto col_idx_bo = make_bo(device, kernel, 2, col_idx);
        auto values_bo = make_bo(device, kernel, 3, values);
        auto x_bo = make_bo(device, kernel, 4, x);
        xrt::bo y_bo(device, y.size() * sizeof(double), kernel.group_id(5));
        auto y_mapped = y_bo.map<double*>();
        std::copy(y.begin(), y.end(), y_mapped);
        y_bo.sync(XCL_BO_SYNC_BO_TO_DEVICE, y.size() * sizeof(double), 0);

        std::cout << "Running krnl_spmv(rows=" << rows << ", scale=" << scale << ", x0=" << x0 << ")\n";
        auto run = kernel(rows, scale, col_idx_bo, values_bo, x_bo, y_bo);
        run.wait();

        y_bo.sync(XCL_BO_SYNC_BO_FROM_DEVICE, y.size() * sizeof(double), 0);

        double max_abs_diff = 0.0;
        for (int i = 0; i < rows; ++i) {
            max_abs_diff = std::max(max_abs_diff, std::abs(y_mapped[i] - expected[i]));
        }

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

        if (max_abs_diff > 1e-12) {
            std::cerr << "ERROR: result mismatch\n";
            return 1;
        }

        std::cout << "PASS\n";
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << "\n";
        return 1;
    }
}
