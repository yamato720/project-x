#ifndef PROJECTX_TESTDATA_CGSOLVER_SPARSE_MATRIX_CSR_HPP
#define PROJECTX_TESTDATA_CGSOLVER_SPARSE_MATRIX_CSR_HPP

#include "Dataset.hpp"

#include <vector>

namespace cgsolver {

class SparseMatrixCsr {
  public:
    explicit SparseMatrixCsr(const Dataset& dataset) : dataset_(dataset) {}

    void multiply(const std::vector<double>& vector, std::vector<double>& out) const {
        out.assign(static_cast<std::size_t>(dataset_.n), 0.0);

        for (int row = 0; row < dataset_.n; ++row) {
            double acc = 0.0;
            for (int offset = dataset_.row_ptr[static_cast<std::size_t>(row)];
                 offset < dataset_.row_ptr[static_cast<std::size_t>(row + 1)];
                 ++offset) {
                acc += dataset_.values[static_cast<std::size_t>(offset)] *
                       vector[static_cast<std::size_t>(dataset_.col_idx[static_cast<std::size_t>(offset)])];
            }
            out[static_cast<std::size_t>(row)] = acc;
        }
    }

  private:
    const Dataset& dataset_;
};

}  // namespace cgsolver

#endif
