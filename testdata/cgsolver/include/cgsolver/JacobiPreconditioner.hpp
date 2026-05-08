#ifndef PROJECTX_TESTDATA_CGSOLVER_JACOBI_PRECONDITIONER_HPP
#define PROJECTX_TESTDATA_CGSOLVER_JACOBI_PRECONDITIONER_HPP

#include "Dataset.hpp"

#include <cmath>
#include <limits>
#include <stdexcept>
#include <vector>

namespace cgsolver {

class JacobiPreconditioner {
  public:
    explicit JacobiPreconditioner(const Dataset& dataset) : dataset_(dataset) {}

    void apply(const std::vector<double>& residual, std::vector<double>& z) const {
        z.assign(static_cast<std::size_t>(dataset_.n), 0.0);

        for (int index = 0; index < dataset_.n; ++index) {
            const double diag = dataset_.jacobi_diag[static_cast<std::size_t>(index)];
            if (std::fabs(diag) <= std::numeric_limits<double>::min()) {
                throw std::runtime_error(
                    "Jacobi preconditioner has a near-zero diagonal at index " + std::to_string(index));
            }
            z[static_cast<std::size_t>(index)] = residual[static_cast<std::size_t>(index)] / diag;
        }
    }

  private:
    const Dataset& dataset_;
};

}  // namespace cgsolver

#endif
