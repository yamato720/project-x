#ifndef PROJECTX_TESTDATA_CGSOLVER_DATASET_HPP
#define PROJECTX_TESTDATA_CGSOLVER_DATASET_HPP

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>

namespace cgsolver {

namespace fs = std::filesystem;

struct Dataset {
    int n = 0;
    int nnz = 0;
    int max_iters = 0;
    double tau = 0.0;
    double check_tolerance = 0.0;
    std::vector<int> row_ptr;
    std::vector<int> col_idx;
    std::vector<double> values;
    std::vector<double> jacobi_diag;
    std::vector<double> rhs;
    std::vector<double> x0;
    std::vector<double> x_expected;
};

template <typename T>
inline std::vector<T> read_array_file(const fs::path& path, std::size_t expected_count) {
    std::ifstream input(path);
    if (!input) {
        throw std::runtime_error("failed to open " + path.string());
    }

    std::vector<T> values;
    values.reserve(expected_count);
    T value{};
    while (input >> value) {
        values.push_back(value);
    }

    if (values.size() != expected_count) {
        throw std::runtime_error(
            "unexpected element count in " + path.string() +
            ": expected " + std::to_string(expected_count) +
            ", got " + std::to_string(values.size()));
    }

    return values;
}

inline Dataset load_dataset(const fs::path& dataset_dir) {
    Dataset dataset;
    std::ifstream meta(dataset_dir / "meta.txt");
    if (!meta) {
        throw std::runtime_error("failed to open " + (dataset_dir / "meta.txt").string());
    }

    std::string key;
    while (meta >> key) {
        if (key == "n") {
            meta >> dataset.n;
        } else if (key == "nnz") {
            meta >> dataset.nnz;
        } else if (key == "max_iters") {
            meta >> dataset.max_iters;
        } else if (key == "tau") {
            meta >> dataset.tau;
        } else if (key == "check_tolerance") {
            meta >> dataset.check_tolerance;
        } else {
            throw std::runtime_error("unknown meta key: " + key);
        }
    }

    if (dataset.n <= 0 || dataset.nnz <= 0 || dataset.max_iters <= 0) {
        throw std::runtime_error("invalid n/nnz/max_iters in meta.txt");
    }
    if (dataset.tau <= 0.0 || dataset.check_tolerance <= 0.0) {
        throw std::runtime_error("invalid tau/check_tolerance in meta.txt");
    }

    dataset.row_ptr = read_array_file<int>(dataset_dir / "row_ptr.txt", static_cast<std::size_t>(dataset.n + 1));
    dataset.col_idx = read_array_file<int>(dataset_dir / "col_idx.txt", static_cast<std::size_t>(dataset.nnz));
    dataset.values = read_array_file<double>(dataset_dir / "values.txt", static_cast<std::size_t>(dataset.nnz));
    dataset.jacobi_diag = read_array_file<double>(dataset_dir / "jacobi_diag.txt", static_cast<std::size_t>(dataset.n));
    dataset.rhs = read_array_file<double>(dataset_dir / "rhs.txt", static_cast<std::size_t>(dataset.n));
    dataset.x0 = read_array_file<double>(dataset_dir / "x0.txt", static_cast<std::size_t>(dataset.n));
    dataset.x_expected = read_array_file<double>(dataset_dir / "x_expected.txt", static_cast<std::size_t>(dataset.n));

    return dataset;
}

inline double dot_product(const std::vector<double>& lhs, const std::vector<double>& rhs) {
    double acc = 0.0;
    for (std::size_t index = 0; index < lhs.size(); ++index) {
        acc += lhs[index] * rhs[index];
    }
    return acc;
}

inline double l2_norm(const std::vector<double>& values) {
    return std::sqrt(dot_product(values, values));
}

inline double max_abs_diff(const std::vector<double>& lhs, const std::vector<double>& rhs) {
    double max_diff = 0.0;
    for (std::size_t index = 0; index < lhs.size(); ++index) {
        max_diff = std::max(max_diff, std::fabs(lhs[index] - rhs[index]));
    }
    return max_diff;
}

inline std::vector<double> vector_subtract(const std::vector<double>& lhs, const std::vector<double>& rhs) {
    std::vector<double> out(lhs.size(), 0.0);
    for (std::size_t index = 0; index < lhs.size(); ++index) {
        out[index] = lhs[index] - rhs[index];
    }
    return out;
}

inline double safe_norm_denominator(const std::vector<double>& values) {
    return std::max(l2_norm(values), std::numeric_limits<double>::min());
}

}  // namespace cgsolver

#endif
